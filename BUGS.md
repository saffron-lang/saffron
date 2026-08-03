# Known Bugs

## Open

**14 open entries:** #2, #49, #65, #66, #75, #107, #115, #117, #123, #128,
#129, #130, #131, #132. Next free number is **#137**.

Everything with a resolution lives under `## Resolved` below, full narrative
intact; `## Fixed` at the end is the older one-line-bullet log. **An entry whose
title says FIXED belongs in `## Resolved`** — if you find one here, move it. That
drift is why this heading is worth keeping honest: for a while `## Open` held 81
entries of which only 14 were actually open, so the count could not be read off
the file at all.

Two titles here are deliberately hedged rather than resolved. #107 says LARGELY
CLOSED because the assertion backfill is incomplete, and #12 was moved to
`## Resolved` as OBSOLETE rather than FIXED — it no longer reproduces, but its
repro targeted the dead C VM, so nothing was fixed for it.

The block numbered 50–57 came out of building the playground; the full narrative
log for that work, including the ones fixed along the way and the workarounds
each forced, is at `docs/design/playground-bug-log.md`. Those entries are under
`## Resolved` now. The log also contains entries that no longer reproduce, noted
there rather than carried forward.

### 110. FIXED — wasm32 never auto-invoked `fun main()`, so a main-only program silently printed nothing

**Severity: high.** Silent, total, and it masks the wasm32 behaviour of at least
seven tests — which means it also hides whatever else is wrong on that target.

Minimal repro — a file whose entire contents are:

```saffron
fun main(): Int { IO.println("inside main"); return 0 }
```

prints `inside main` natively and **nothing at all** on wasm32. Adding an
explicit `main()` call at top level makes both agree.

`output_body.sf:1276` emits the `__saffron_boot` shim only when
`has_top_level`, but `wasm_base_32.ll:2188`'s `_start` calls `@__saffron_boot()`
unconditionally. A main-only program has no top-level statements, so no shim is
emitted, `_start`'s call resolves to an undefined symbol, and
`--import-undefined` turns it into a silent no-op import. Nothing fails; the
module simply does nothing.

The native path one screen away is correct, which is what makes this a drift
rather than an oversight: line 1301 guards on
`this.str_in_list(this.defined_funcs, "__saffron_main") or has_top_level`, i.e.
it has the explicit `__saffron_main` branch that the wasm path lacks. The fix is
to give the wasm branch the same case.

Found while A/B-ing the #109 fix: all seven wasm32 mismatches in a full
`tools/differential.sh` sweep share this one signature — wasm32 emits nothing —
and none of them are value-layer bugs. Worth fixing before the remaining
value-layer drift, since it currently makes wasm32 output unobservable for those
seven programs.

**Fixed (2026-08-02)** by widening the wasm `__saffron_boot` guard at
`output_body.sf` to match the native wrapper's:
`this.str_in_list(this.defined_funcs, "__saffron_main") or has_top_level`, plus a
second shim body for the main-only case. That body must call `__saffron_main`, not
`__saffron_entry` — `__saffron_entry` is emitted only under `has_top_level`, so
calling it here would reintroduce exactly the undefined-symbol-becomes-silent-import
failure this fixes — and it runs the module inits itself, since with no top-level
code there is no `__saffron_entry` for them to run inside. The async-main case
enqueues the coroutine frame (matching the wasm top-level coroutine arm, not native:
the host event loop cannot be blocked).

The decisive check is running, not linking — the agent's whole framing is that the
defect *was* that it linked clean. `tools/saffron build test/pass/main_entry_only.sf
--target wasm32` then `node tools/oracle/wasm_run.mjs`: **before, empty; after,
`main-only entry ran`**. Three regression tests added
(`main_entry_only`, `main_entry_and_top_level`, `top_level_entry_only`), each with a
`.expected`; the main-only file is kept free of any top-level call because a single
one flips `has_top_level` and tests the branch that was never broken.

Two consequences followed. The `main_entry_only()` capability gate in
`tools/differential.sh` was dead — the 11 `fun main`-only programs it SKIPped are
graded again. And the "seven wasm32 mismatches" that motivated this entry were
partly stale: the 17 remaining differential mismatches are a *different* signature
(no `fun main` at all). Two adjacent wasm64 defects surfaced while confirming the
fix and are filed as their own entries: #124 (wasm64's `_start` calls
`__saffron_entry` directly with no `__saffron_boot` indirection, so the same
main-only program never runs there either) and #125 (`wasm_base.ll` has no single-arg
`__io_println` NaN-box dispatcher, so all wasm64 output is dropped regardless).

### 107. LARGELY CLOSED — 43 positive tests asserted nothing and would pass on any output

**Severity: high — this is the reason the other entries survived.**

| | count |
|---|---|
| assertions **and** `.expected` | 0 |
| assertions only | 77 |
| `.expected` only | 33 |
| **neither — passes on ANY output** | **64** |

The 64 are graded solely on "exit 0 and no `Runtime Error:` on stderr". They
print, and nothing checks what. Includes all 11 `mini_*` tests,
`test/pass/enums.sf`, `test/pass/generics.sf`, `test/pass/closures.sf`,
`test/pass/interfaces.sf`, `test/pass/operators.sf`, `test/json.sf`,
`test/inheritance.sf`.

Not hypothetical: `test/pass/enums.sf` and `test/pass/generics.sf` print
subnormal doubles today (#105) and both pass. `run_tests.sh` already notes that
`test_async.sf` "was green for the entire life of BUGS #38 while emitting 2 of
its ~12 expected lines and garbage for the rest" — the mechanism was known, the
scale was not.

`tools/differential.sh --record` writes `.expected` from the reference run and
refuses when configurations disagree.

---

**LARGELY CLOSED (2026-08-02).** The blind set was re-derived independently rather
than trusting the "64" above, which predated several merges. At the time of the
work: **184 positive files — 0 with both mechanisms, 86 assertions-only, 55
`.expected`-only, 43 blind.**

After the pass: **23 still blind, and 21 of those already FAIL in the baseline**
(build-fail / timeout / segfault / runtime-error). A test that produces no output
cannot enshrine a wrong one, so those 21 are not the hazard this entry is about.
Only **2 are green-and-blind**, both deliberately.

Coverage added across 27 files, via three mechanisms: 21 `.expected`, 2 empty
`.expected` (which pin the two import helpers to printing *nothing*), 4 `.exit`
(a **new** mechanism), 5 files given assertions, and 1 carrying both. Every
recorded output was read against the source before acceptance, then cross-checked
through the oracle: **36 AGREE, 0 MISMATCH** across native-O2 / native-O0 /
wasm32.

**What was deliberately NOT recorded is the more important half.**
`test/pass/enums.sf` and `test/pass/generics.sf` print `0` and `7.29112e-304` from
`IO.println(Color.Red)` — that is #105. Recording it would have frozen a bug as
intended behaviour and made this entry worse, so each instead got 10 assertions
via *interpolation* (which inserts `.to_string()` and is correct), leaving every
`IO.println` untouched so the garbage stays visible.

`gc_coro_root_{depth,order,overpop}.sf` were also left unfrozen, and this one was
*proven* rather than argued: adding the three locals needed to capture shadow-stack
depths moved the reported value 18 → 24. The depth is frame-layout dependent, so
freezing it would flag unrelated codegen as a regression. Their *invariants* are
asserted instead — depths equal across rounds, each frame above its spawner.

**Three harness defects found and fixed, all of which were this entry's own
mechanism one layer up:**

1. **The oracle compared stdout only.** It set `RUN_EC` per config and never used
   it. The four `mini_*` tests print nothing, so all four "AGREEd" by matching two
   empty files — a wasm32 run returning 0 where native returned 55 was a unanimous
   pass. It now compares exit status too, verified by injecting `RUN_EC=7`.
2. **`output-mismatch` was missing from `run_tests.sh`'s category breakdown**, so a
   `.expected` regression counted in the total but vanished from the summary.
3. **The two grading mechanisms compose**, contrary to what "0 files with both"
   suggested: the assertion gate returns early *only on failure*, so passing
   assertions fall through to the diff. The 0 was an accident, not policy;
   `interfaces.sf` is now the first file carrying both.

Also: `test/gc_deep_test.sf` was stable over **25 consecutive runs**, so the
"nondeterministic" label it carries elsewhere in this file is doubtful and should
not be used to dismiss a failure there.

**Merge hazard worth recording here, because it makes a fix look like it did not
work:** `tools/saffron` links the **checked-in** `build/stage3/runtime.ll`
(`tools/saffron:16`), not `src/runtime/runtime.sf`. So a merge that brings a
`runtime.sf` fix without regenerating that artifact leaves the fix **inert** —
`test/pass/subnormal_not_a_pointer.sf` still segfaulted (exit 139) on a tree that
already contained its fix in source. Regenerating `build/stage3/runtime.ll` made
all 13 assertions pass. Any runtime change therefore needs the artifact rebuilt
before its tests mean anything, and a green test run against a stale artifact is
evidence of nothing.


### 75. A value that re-enters wasm from JS is untagged, so it can never match a `Map` key it was stored under

**Reproduction** (wasm32; `t.sf`):

```saffron
@extern("void js_note(i64)")
fun _note(id: Float)

var _next_id: Float = 0
var _cb: Map<Float, String> = {}

fun reg(payload: String): Float {
    _next_id = _next_id + 1
    var id: Float = _next_id
    _cb.set(id, payload)
    return id
}

fun kick() { _note(reg("HELLO")) }

// Exported to JS (the `__on_` prefix is what tools/saffron:392 exports).
fun __on_back(id: Float) {
    if (_cb.has(id)) { IO.println("FOUND " + _cb.get(id)) }
    else { IO.println("MISSING") }
}

kick()
```

Build with `tools/saffron build t.sf --target wasm32 -o t.wasm`, then from JS call
`_start()` (which invokes `js_note` and hands you the id) and pass that exact id
straight back to `__on_back`:

```
js_note got id = 1n
MISSING
```

The id makes a round trip and stops being findable. Handing the *bit pattern of
1.0* back instead prints `FOUND HELLO`, which localizes it exactly:

```js
const bb = new ArrayBuffer(8); new Float64Array(bb)[0] = 1.0;
inst.exports.__on_back(new BigInt64Array(bb)[0]);   // FOUND HELLO
```

So the two representations of "1" involved are:

| Path | Value seen by `__map_key_cmp` |
|---|---|
| stored by `reg()` | `0x3FF0000000000000` — the f64 bit pattern of 1.0 |
| arriving from JS | `0x0000000000000001` — a bare machine integer |

An exported wasm function's `i64` parameter is whatever JS passed; nothing on the
boundary re-tags it into the NaN-boxed form the module uses internally. And
`__map_key_cmp` (`src/runtime/runtime.sf:287`) compares non-string keys by exact
bit pattern — correctly, for its own purposes — so the lookup misses. Note
`__val_untag_float` (`src/runtime/wasm_base_32.ll:763`) would convert `1` to `1.0`
happily; it is simply never called, because a `Map` key is compared as an opaque
i64 and never untagged at all.

**Why it is nastier than a plain type-mismatch:** every symptom is silent. There
is no diagnostic at compile time, no trap at runtime, and `has()` returning false
is a legitimate result, so the callback is *dropped* rather than failing. In the
playground this meant the Run button POSTed correctly, the service replied
correctly, and then nothing happened at all — no error in the console, no status
change. The pattern it breaks (hand JS an opaque id, get it back later) is the
only way to do asynchronous work across the wasm boundary, so any callback
registry written the obvious way is affected.

Also note `Bool.to_string()` on wasm32 prints `"false"` for `true` — a separate,
unfiled printing bug (the branch `if (t)` is taken correctly). It matters here
because it made an early `IO.println(_cb.has(id).to_string())` read as a Map
failure in cases where the Map was fine, and it will mislead anyone debugging this
by printing booleans.

**Workaround (in use):** index a `List` instead of keying a `Map`. An index only
has to compare numerically, which survives the representation change:

```saffron
var _callbacks: List<(String) => Nil> = []
fun _register(cb: (String) => Nil): Float {
    _callbacks.push(cb)
    return _callbacks.length() - 1
}
fun _resolve(id: Float, payload: String) {
    if (id >= 0 and id < _callbacks.length()) {
        var cb: (String) => Nil = _callbacks[id]
        cb(payload)
    }
}
```

Applied at `playground/frontend/src/api.sf`. This is also why Turmeric's own
`__dispatch_event` (`turmeric/src/prelude/03_callbacks.sf:27`) is List-based —
it works, but the reason was not written down, so the Map version looks fine
until you try it.

The real fix is to normalise incoming values at the export boundary: codegen knows
each exported function's declared parameter types, so a `Float` parameter on an
exported function should be re-tagged (`__val_tag_float` on the integer, or a
tagged-vs-raw check like `__val_untag_int` already does) in the function prologue.
Until then, no NaN-boxed value should be used as a `Map` key if it crosses the JS
boundary.

### 66. Binary files cannot be read or served — `IO.read_file` truncates at the first NUL, so `static_files` serves any wasm module as 0 bytes

Found while verifying the playground before committing it. `GET /app.wasm` returns
`200` with `Content-Length: 0` even though `playground/static/app.wasm` is 19569
bytes on disk. The playground frontend therefore cannot load in a browser at all.

**Reproduction** — no server needed:

```saffron
import "io" as IO

// 13 bytes on disk, starting with the wasm magic \0asm
var leading: String = IO.read_file("/tmp/nul_test.bin")
IO.println(leading.length())   // 0

// "abc\0def", 7 bytes on disk
var mid: String = IO.read_file("/tmp/nul_mid.bin")
IO.println(mid.length())       // 3
```

Create the fixtures with `printf '\0asm\x01\x00\x00\x00hello' > /tmp/nul_test.bin`
and `printf 'abc\0def' > /tmp/nul_mid.bin`.

**Cause.** `__io_read_file` (`src/runtime/runtime.sf:1096-1098`) is *not* the
problem — it `rt_fread`s the full size and returns the whole buffer. The loss is
in the representation: a Saffron `String` is a NUL-terminated C string, so
`length()` is a `strlen` that stops at the first embedded NUL. Every byte after it
is still in memory but unreachable. A wasm module's magic number is `\0asm`, so the
very first byte terminates the string and the read yields `""`.

**This is wider than the read.** The `@http/server` response path is String-typed
end to end, so a binary body is unrepresentable even given a correct read:

- `src/lib/http/server.sf:655` — `static_files` reads with
  `var content: String = IO.read_file(full_path)`
- `:168` — `Response.body` is declared `String`
- `:251-252` — `Content-Length` is emitted from `resp.body.length()`, i.e. strlen
- `:269` — the body is appended to a `StringBuilder`

So fixing only `read_file` would still emit a truncated `Content-Length` and a
truncated body. Serving binary assets needs a byte-length-carrying body type
(`@bytes` `Buffer`, or a `String` that stores an explicit length) threaded through
`Response` and the writer.

**A working read path already exists** and is the basis for any fix:
`IO.file_size` + `IO.read_binary` (`src/lib/io.sf:255-263`, runtime
`:1103-1134`) return the correct 13 for the fixture above. Note they currently
emit `[codegen] Warning: calling undefined function '__io_file_size'` /
`'__io_read_binary'` — the functions exist in `runtime.sf` and link fine, but
they are missing from the codegen known-function tables in
`src/compiler/codegen/utils_body.sf:5-6`, so every call warns.

**Relationship to the log.** This is the same underlying defect as Bug 2 in
`docs/design/playground-bug-log.md`, which was only ever *worked around* inside the
playground's compile endpoint and never filed in the tracker. This is a second,
independent instance of it, so it is filed here as the general defect.

**Note on the playground handoff**: the completing agent's report listed
`/app.wasm` among "All 6 routes correct". That claim does not hold — the route
returns 200 with an empty body.

**Status: still open as a stdlib defect; routed around in the playground.** The
underlying defect is unchanged — `static_files` still cannot serve a wasm module,
and `Response.body` is still String-typed — but the playground no longer depends
on it. It serves the UI module base64-encoded from its own endpoint
(`GET /api/app_wasm`, `playground/src/main.sf`), which the loader decodes with
`atob` before instantiating; base64's alphabet is pure ASCII, so it survives a
Saffron string intact. This is the same dodge the compile endpoint already used
for user modules, now factored into `Compile.encode_file_base64`. Verified
byte-identical: the 47555-byte module round-trips through the endpoint and the
frontend loads and runs. Costs a 33% larger transfer once per page load.

That does not fix the general case — any Saffron program serving a binary asset
still hits this — so the byte-length-carrying body type described above is still
the work that needs doing.

**FIXED** (`e6b0735`). `IO.Bytes` is that body type: an explicit (pointer, length)
pair, so length is carried rather than recomputed by scanning for a NUL, plus
`read_file_bytes` / `write_file_bytes`. `Response` carries an optional
`_body_bytes` which takes precedence for `Content-Length`, and `_write_response`
writes the body straight from its pointer via `sf_tcp_write`/`sf_tls_write`
instead of through a `String`. `static_files` reads byte-exactly. Fixing the read
alone would not have been enough — the response path was `String`-typed end to
end, so `Content-Length` was a strlen and the payload went through a
`StringBuilder`.

Built on `_b_*` C stdio externs rather than the runtime's `__io_file_size` /
`__io_read_binary`, because those two are absent from codegen's known-function
table: calling them from inside a module gets them module-prefix-mangled to
`@io___io_file_size` and they fail to link.

Those externs declare `FILE*` as `i64`, not `void*`, deliberately. **A
`void*`-returning extern has its result pointer-tagged, so `fp == 0` compares
`TAG_PTR|0` against `TAG_INT|0` and is never true** — a failed `fopen` would take
the success branch and read through a NULL `FILE*`. `IO.open` has exactly this
latent defect and does not throw on a missing file; that is unfixed and worth its
own entry.

Tests: `test/pass/binary_file_bytes.sf`, `test/pass/http_binary_response.sf`.
Still uncovered: the TLS byte-write path is implemented but untested, and
Stream/SSE remains String-only.

### 65. Runtime errors are fatal, not catchable — four docs promised the opposite

`try`/`catch` works correctly for `throw`. It does **not** catch runtime faults:
`IndexError`, `DivisionError` and `NullError` all route through
`__runtime_error_fatal` (`src/runtime/runtime.sf:713`), which writes to fd 2 and
calls `rt_exit(1)`. No `catch` or `finally` block runs.

Repro — the exact example `CLAUDE.md` used to carry:

```saffron
fun main() {
    try {
        var list = [1, 2, 3]
        IO.println(list[99])
    } catch (e) {
        IO.println("caught: ${e}")
    }
    IO.println("still alive")
}
main()
```

Actual: prints `Runtime Error: IndexError: index 99 out of bounds (length 3)`,
exits 1. Neither `caught:` nor `still alive` is reached. Division by zero behaves
identically. An explicit `throw` in the same shape is caught and returns 0, which
is what made the wrong claim plausible for so long.

The fatal call sites are `runtime.sf:777` (`__index_error`), `:815`
(`__division_error`) and `:857` (`__null_pointer_error`).

Nil misuse mostly never gets this far: the checker rejects calling a method on a
nullable value at compile time.

**Docs fixed** (2026-07-30): `CLAUDE.md`, `docs/src/tutorial/error-handling.md`,
`docs/learnxinyminutes/learnsaffron.sf` and its `index.html` all claimed runtime
errors were catchable and showed output that never occurs. They now document the
fatal behaviour and show a guard-before-indexing pattern instead.

**Still open**: whether the *behaviour* should change. Making these catchable is
not a small fix — the error path itself allocates (`__runtime_error` calls
`rt_malloc`), and `setjmp`/`longjmp` are no-ops on both wasm bases
(`wasm_base_32.ll:592-597`, `wasm_base.ll:503-508`), so `try`/`catch` cannot work
on wasm at all. Decide whether fatal-by-design is the intended semantics before
investing there.

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

**Progress (2026-07-30).** The retirement is underway; the mapping is unchanged
but its blast radius is shrinking:

- `src/lib/*.sf`: all 134 `Number` annotations converted. Bulk → `Int`
  (indices, counters, ports, handles, lengths, byte values, char codes);
  `scheduler.sleep_times` → `List<Float>`; `reflect.number_to_string`,
  `toml.number`/`number_or` → `Any` (genuinely int-OR-float pass-throughs that
  do no arithmetic). `toml.sf`'s `peek_at(offset: Number)` — the example cited
  above — is now `offset: Int`.

  **Correction (2026-08-01): "no `Number` annotation remains in `src/lib`" was
  false when written.** Fourteen live annotations were left in
  `src/lib/http/client.sf` and `src/lib/http/server.sf` — `var status`,
  `max_redirects`, `redirect_count`, `port`, `body_start`, `_parse_hex(): Number`,
  `_index_of_from(start: Number): Number`, `_is_redirect(status: Number)`, and
  five `var i`/`var k` loop counters. All are indices, counts or HTTP status
  codes, so all fourteen became `Int`. The five remaining hits in
  `src/lib/llvm/test_codegen.sf` are inside comments and are left alone. `src/lib`
  is now genuinely free of `Number` annotations.
- `test/*.sf`: all 152 annotations → `Int` across 49 files. The 9 `is Number`
  checks are kept deliberately: they cover the feature while it still exists.
- `checker.sf` no longer collapses `Float` into `IntType` when parsing type
  strings, so `Int` and `Float` are finally distinct in the checker. This alone
  turned two `test/fail/` cases from "compiled cleanly" (itself a failure) into
  correct rejections, taking the suite from 90/47 to 92/45.
- A latent runtime bug surfaced and was fixed on the way: `__val_is_float`
  called every raw untagged heap pointer a float, so rewriting `is Number` to
  `is Int or is Float` made `JSON.to_string({...})` serialize a Map as an
  integer. `is Number` had masked it by only ever checking the int tag — which
  also means **`is Number` returns false for every float**, so all seven stdlib
  `is Number` guards were already silently wrong. See the Fixed section.

**Remaining:** user-facing docs, then removing the surface spelling from the
lexer/parser/checker/codegen — which is a breaking change for user programs and
wants its own decision.

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

### 115. A class instance reaching a formatter prints its raw bit pattern

**Severity: high.** Silent wrong answer. The class-shaped sibling of #105, with a
*different* mechanism, so #105's fix does not reach it.

```saffron
class Pt { var x: Number
           fun init(x: Number) { this.x = x }
           fun to_string(): String { return "Pt(${this.x})" } }
var p: Pt = Pt(3)
IO.println(p)          // 5.21502e-310   — WRONG, and this is TOP LEVEL
IO.println("${p}")     // Pt(3)          — correct
var ps: List<Pt> = [Pt(1), Pt(2)]
IO.println("${ps}")    // [5.21502e-310, 5.21502e-310]
```

Verified directly at HEAD, not merely reported: `IO.println(p)` prints
`5.21502e-310` while `"${p}"` prints `Pt(3)`.

#105 was cured by `methods_body.sf:1543` inserting `<Enum>__to_string()` when the
argument's **static** type is a known enum (`enum_defs.has(type)`). That is
static-type-driven, so it covers exactly what the checker can see through and
nothing else — no list element, and **no class arm at all**.

**The runtime cannot fix this alone, and the class case is what proves the
prerequisite is insufficient.** A class instance *does* carry a GC header
(`__gc_alloc(size, class_tag)`, tags allocated from 10) and `__val_class_tag`
reads it safely — so unlike a fieldless enum, the value **is** identifiable. It
still prints as bits. What is missing is the mapping from tag to that class's
`to_string()`, which only codegen holds (`class_type_ids`, `class_own_methods`).
So "give payload enums a GC header" is necessary but *not* sufficient for the
enum half either.

Proposed fix: emit a tag-switch `__val_to_string(i64)` in
`emit_class_hierarchy_helpers()` (`stmts_body.sf:1740`), beside the
`__class_parent_tag` / `__class_is_a` switches it already generates from exactly
these tables, and call it from `__any_to_string` (`base_nanbox.ll:1355`) before
the `do_float` fallthrough. Payload enums join by allocating via `__gc_alloc` in
`gen_enum_construct` (`expr_body.sf:3101`) instead of `__sf_malloc`, with tags
from the same allocator as `next_class_type_id` (`codegen.sf:198`) to avoid
collision. `__val_to_string` must return a raw `char*` like its siblings — no
tag/untag step, which is what would re-create #102's segfault.

**Fieldless enums cannot join this scheme.** `tag << 56` is a bare immediate with
no allocation and no identity; there is nothing to key on. A bit-pattern heuristic
was deliberately not written, because a wrong guess is worse than visible garbage.

Two caveats to accept explicitly if this is implemented: `__any_to_string` is
**absent from `wasm_base.ll`** (0 definitions; the other three bases have it), so
**wasm64 silently loses the behaviour**; and the emit site is gated
`if (!this.identity_mode)`, so **bootstrap can never validate it** — the
identity-mode blind spot again.

Regression test `test/oracle_println_class_not_bits.sf` is **6/8, failing on
purpose**. The garbage is an address reinterpreted as a subnormal, so it varies
per run and per `-O` level; recording it would make the eventual fix look like a
regression (#107 discipline). Working paths are asserted positively as the
reference; broken paths only negatively.

---

### 117. An unannotated list literal of class instances loses its element type

```saffron
var ann: List<Pt> = [Pt(1), Pt(2)]
IO.println(ann[0].to_string())   // Pt(1)          — correct
var un = [Pt(1), Pt(2)]
IO.println(un[0].to_string())    // 5.21502e-310   — WRONG
```

Verified at HEAD: the same expression answers correctly with an annotation and
returns the raw pointer without one. **Minor severity only because the annotation
is a workaround** — the wrong answer is silent.

Distinct from #115: this is the untyped-receiver dispatch hazard (a call on a
value whose type inference failed is dropped or misdispatched rather than
diagnosed), and it is *why* `test/oracle_println_class_not_bits.sf` annotates its
lists. It also means #115's "only nested elements are wrong" framing holds for
**annotated** collections only.

---

### 121. FIXED — a variable used only in callee position was falsely reported as an unused variable

**Severity: low** — warning-only, no codegen consequence. Filed because it is a
*false* diagnostic, and a warning that fires on correct code is how people learn to
ignore warnings.

```saffron
fun run(): Int {
    var fs: List<Fun> = [fun (x: Int): Int => x]
    return fs[0](7)            // fs IS used, right here
}
```

```
[checker] Warning: unused variable 'fs'
```

`infer_call` (`checker.sf:2161`) pattern-matched the callee only to extract a *name
string* for the return-type lookup, and never called `infer_expr` on it. Since
`mark_used` is reachable only from the `Variable`/`Ref` arms of `infer_type`
(`checker.sf:1701,1705`), a variable appearing **only** in callee position was never
marked used.

The axis is "appears only as a callee", **not** "has a function type" — which is
what makes this its own bug rather than part of #120. Two observations pin that
down: a `List<Fun>` variable, which is not a function type at all, warns when its
only appearance is `fs[0](7)`; and the same variable used as a plain value
(`var alias: Fun = h`) is correctly marked used.

**Fixed (2026-08-02)** by `this.infer_expr(callee)` in `infer_call`. The narrower
`mark_used(name)` route was rejected on measurement, not preference: (a) the shapes
that actually trigger the bug — `IndexGet` (`fs[0](7)`), `Call` (`maker(3)(4)`),
`MethodCall`, parenthesized — all take `infer_call`'s `_ => ""` arm, so there is no
name to mark and the headline repro would have stayed broken; (b) for a
`MemberAccess` callee the extracted name is the *field*, and `mark_used` searches
scopes by string, so `(b.cb)(1)` would have marked an unrelated local `cb` used and
swallowed a warning that must fire — now a must-still-warn assertion. The walk also
closes a silent gap: a nested callee's own arguments (`f(a)(b)`) were not checked at
all before, and now are, exactly once.

Diagnostics-only, verified over 332 files: emitted `.ll` byte-identical for all 332;
24 diagnostic files differ — 21 removals each traced to a callback whose only use is
`func(item)`-shaped, 3 line-number shifts of a pre-existing parse error, zero new
warnings. Failure set unchanged (24 names before and after). The tests shell out to
`build/saffronc` because `run_tests.sh` strips `[checker] Warning` lines, so a pass
test cannot observe a warning about itself; against the pre-fix compiler 5 of the 8
must-not-warn assertions fail, so they detect the defect rather than merely passing
alongside the fix.

The "top level does not warn" wrinkle turned out to be orthogonal: every
`check_unused_in_scope()` call sits inside a `push_scope`/`pop_scope` pair and
`check_program` never runs one for scope 0, so top-level variables are never checked
for unusedness at all. Two adjacent defects were left as their own entries: #126 (a
lambda body is never walked, so a capture-only variable draws the same false warning
by a different missing walk) and #127 (`checker.sf` does not parse standalone).

---
### 122. FIXED — assigning to a nonexistent module member compiled clean and emitted invalid IR

**Severity: high**, for the same reason #118 was: exit 0, no diagnostic, and the
failure that eventually surfaces blames the compiler for the user's typo.

```saffron
import "@math" as Math
Math.NOPE = 3          // exit 0, no diagnostic
```

```
%t1 = load i64, i64* %Math
```
`opt` then rejects the module (`invalid getelementptr indices` on the `%Iterable`
GEP built from that undefined load).

This is the write half of #118, left behind by that fix. #118 hardened the
member-*read* path in `gen_arg_value`, where a module alias with no matching member
now reports absence. Assignment never reaches that block, so it still falls through
to the generic receiver path, which evaluates the bare alias `Math` as though it
were a local and loads from an SSA name that was never defined.

Verified as **pre-existing, not a regression**: the same file gives exit 0 and the
same single `load i64, i64* %Math` on both the pre-#118 and post-#118 compilers.
Found by sweeping 14 syntactic positions while checking which of #118's two
candidate sites actually fires — every position emitted the new diagnostic except
this one, which is what made the gap visible.

Ninth instance of the can't-express-unknown family (#22, #40, #78, #37, #103, #113,
#114, #118): a resolver with no way to say "not found" guesses instead of reporting
absence. That #118's fix closed the read path and not the write path is the
recurring shape of this family in miniature — the guess lives in the *fallback*, so
every distinct path that reaches the fallback needs its own report, and closing one
does not close the others.

The fix wants the same treatment as #118: at the point where the alias is known to
be a module and the member lookup has missed, report rather than fall through.

**Fixed (2026-08-02)** by `report_missing_module_write` (`expr_body.sf:3315`),
called above the shared `gen_set_field` while the alias is still known to be a
module. `Math.NOPE = 3` went from exit 0 / no output / `%t1 = load i64, i64* %Math`
(which `opt` rejects) to `[codegen] Error: no member 'NOPE' in module 'Math'`, exit
1. `Math.pi = 3.0` and a class field write are unaffected, exit 0 before and after —
verified on both the pre-fix and post-fix compilers.

**Two write sites, one live — measured, as #118's read pair was.** Each got a
distinct marker string; the compiler was rebootstrapped and 18 syntactic positions
swept (top level, function body, if/else, while, for-in, C-style for, try, catch,
match arm, class method, `init`, nested `fun`, actor method, bare block, two writes
in a row). `gen_arg_value`'s `set_field` branch (`expr_body.sf:1698`) fired in all
18; the `SetField` arm of `gen_expr` (`:399`) fired in none — `gen_expr`'s four
callers are all inside `gen_arg_value`, which handles `set_field` first. Both
hardened, the dead one annotated. The readable arm being the dead one is the same
trap as #118.

**A second hole, closed in the same guard.** A member that *exists* but is not a
variable reached the identical fall-through: `Math.abs = 1`, `Iter.sum = 1`,
`IO.println = 1`, `GC.collect = 1`, a module class `Inner.Widget = 1`, a module enum
`Inner.Colour = 1` — each gave exit 0 and invalid IR. Reporting only *absence* would
have left all six open, since the fallback does not care *why* the lookup produced
nothing. They now get `cannot assign to '<x>' in module '<y>' — it is not a
variable`, a distinct message because a typo and a category error send the user to
different places.

Tenth instance of the can't-express-unknown family (#22, #40, #78, #37, #103, #113,
#114, #118), and the second within #118's own mechanism: a resolver with no way to
say "not found" guesses instead of reporting absence. The guess lives in the
*fallback*, so every distinct path that reaches it needs its own report — closing
the read path (#118) did not close the write path.

The compound-assignment and increment spellings this entry asked about are **not
#122 sites**: `Math.NOPE += 1` and `Math.NOPE++` are rejected by the parser because
the lexer has no compound-assignment or increment token at all, so they never reach
the fallback. Filed separately as #128. `Math.NOPE[0] = 1` and `Math.NOPE.deeper =
1` were already caught by #118's read guard, since both evaluate their base as a
read.

Tests: `test/fail/module_member_write_missing.sf`,
`test/fail/module_member_write_not_variable.sf`,
`test/pass/module_member_write_valid.sf` (9 assertions, including a read-back that
proves the store landed rather than being reported and skipped). Both `fail/` files
compiled cleanly on the pre-fix compiler and are rejected after, so they detect the
defect. Failure set unchanged at 24 names.

---

### 123. Three stdlib files bind different modules to the same alias `Internal`, so the alias resolves to whichever wins

**Severity: medium** — it currently breaks exactly one file, and that file has no
importers. The mechanism is what matters: an import alias is not scoped to the file
that declares it the way the syntax implies.

```
src/lib/ast.sf:13     import "../compiler/ast.sf"    as Internal
src/lib/lexer.sf:9    import "../compiler/lexer.sf"  as Internal
src/lib/parser.sf:10  import "../compiler/parser.sf" as Internal
```

`src/lib/lang.sf` imports all three (`@ast`, `@lexer`, `@parser` at lines 13–15).
The alias collides, so `Internal.Token` inside `lexer.sf` resolves against
`compiler_ast_Token` rather than `compiler_lexer_Token`, and misses:

```
[codegen] Error: no member 'Token' in module 'Internal'
[codegen] Error: no member 'TokenKind' in module 'Internal'
```

**Not a regression from #118.** Verified on the pre-#118 build, where `lang.sf`
already emitted invalid IR — `use of undefined value '%Internal'` — for the same
reason. #118's fix converts a silent invalid-IR failure into a named diagnostic, so
`lang.sf` moves from exit 0 to exit 1 while being equally broken either way. It was
never working; it was failing invisibly.

`@lang` has no real importers — the three hits for `"@lang"` in the tree are all
commented-out documentation lines (`//! import "@lang" as Lang`), including the one
in `lang.sf` itself. So no test moves, and nothing downstream breaks. That is also
why this survived: the file is dead weight that nothing compiles.

Two separable questions here, and the second is the important one:

1. The immediate fix is to give the three files distinct aliases (`InternalAst`,
   `InternalLexer`, `InternalParser`). Cheap, and it makes `lang.sf` compile.
2. The real defect is that an alias declared in one file can be shadowed by an alias
   of the same name declared in another, and the collision is silent. An alias should
   either be file-local (the reading the syntax suggests) or a collision should be
   diagnosed. Renaming the three aliases fixes this instance and leaves the trap set
   for the next one. Whichever way it is resolved should be a decision about alias
   scoping, not a rename.

Found while measuring #118's blast radius across 336 files.

---

### 124. FIXED — wasm64's `_start` called `__saffron_entry` directly, so a main-only program never ran there either

**Severity: high**, and the same shape as #110 by a different mechanism. #110 was
the wasm32 half — its `_start` calls `@__saffron_boot()` and the shim was not
emitted for a main-only file. wasm64 does not go through `__saffron_boot` at all:
`wasm_base.ll:1028` declares and `:1039` calls `@__saffron_entry` directly, and
`__saffron_entry` is emitted by codegen only under `has_top_level`. So a file whose
entire contents are `fun main()` links against an undefined `__saffron_entry` on
wasm64, which `--import-undefined` turns into a silent no-op host import — the module
builds, runs, and does nothing.

`grep -c __saffron_boot src/runtime/wasm_base.ll` is **0** (wasm32 has 3), which is
the structural difference: the #110 fix widened the guard on the `__saffron_boot`
shim, but wasm64 has no such indirection to widen. The fix is to route wasm64's
`_start` through the same `__saffron_boot` shim the #110 fix now emits, or to emit a
`__saffron_entry` for the main-only case on this target too.

Found while confirming the #110 fix on wasm32; the sibling target was checked at the
same time and had the same disease through a different pipe.

**Fixed (2026-08-02)** by routing wasm64's `_start` through `@__saffron_boot()`, the
same indirection wasm32 uses (`wasm_base_32.ll:2179`). **No codegen change was
needed** — `output_body.sf:~1294`'s guard already covers `wasm64`, and its main-only
branch already emits module inits then calls `__saffron_main`; the shim was being
emitted all along and simply never called. Chosen over "emit `__saffron_entry` for the
main-only case" for two reasons: a coroutine `__saffron_entry` returns `ptr`, not
`i64`, whereas `__saffron_boot` is a stable `i64 ()` shim that already handles async
main (enqueue without `scheduler_run`); and convergence removes the divergence that
let this survive the #110 fix in the first place.

Verified by **running** the module, not by linking it — the defect was that linking
succeeded. `test/pass/main_entry_only.sf` built for wasm64 and run under
`node tools/oracle/wasm_run.mjs` printed nothing before and prints
`main-only entry ran` after.

---

### 125. FIXED — `wasm_base.ll` had no single-arg `__io_println` dispatcher, so all wasm64 output was silently dropped

**Severity: high.** Independent of #124 — even a program that *does* reach its
print calls emits nothing on wasm64.

`grep -n 'define i64 @__io_println(i64' src/runtime/*.ll` finds it in
`base.ll:99`, `base_nanbox.ll:106` and `wasm_base_32.ll:1740`, and **not** in
`wasm_base.ll`. That single-argument entry is the NaN-box dispatcher every
`IO.println(x)` call lowers to; with no definition on wasm64 the symbol is undefined
and `--import-undefined` makes the call a no-op host import. So wasm64 output is
dropped whether or not #124 lets `main` run at all.

The fix is to port the `wasm_base_32.ll:1740` definition into `wasm_base.ll`,
adjusting only the pointer width. Filed separately from #124 because they are
distinct missing symbols with distinct fixes, and #124 alone would leave output
still silently dropped.

Found while confirming the #110 fix — the wasm64 build of the same regression test
linked clean and printed nothing even after #124's mechanism was understood, which
is what surfaced this second missing symbol.

**Fixed (2026-08-02), but this entry's stated fix was WRONG and that is the lesson.**
It said to port `wasm_base_32.ll:1740` "adjusting only the pointer width." There is
no pointer width to adjust — `grep i32` over that block returns **nothing**. The two
bases differ in **value discipline**, not pointer size: `src/runtime/values.spec:41-44`
declares `wasm64 discipline=identity` and `wasm32 discipline=nanbox`.

The verbatim nanbox port was built and measured rather than reasoned about: it links
clean and prints `0` for every value. Mechanism — an identity i64 bitcast to double is
a **denormal**, and `fptosi` truncates every denormal to 0. That is the `untag_int`
trap in a new location. The identity shape from `base.ll` (the other identity-discipline
base) was ported instead.

**A "port the working definition from the sibling target" instruction is only safe when
the two targets share a value discipline.** Check `values.spec` before believing one.

Honest limitation, documented in `wasm_base.ll`: on wasm64 only `String` prints
correctly. `scratch/w64/battery.sf` run on all three targets shows native and wasm32
printing all six values while wasm64 prints the two strings and blanks the four
non-strings — a real improvement over printing nothing at all, but not parity. The
remaining half is #131.

---

### 126. FIXED — a lambda body was never walked, so a capture-only variable was falsely reported unused

**Severity: low** — warning-only, the same false-diagnostic class as #121, at a
different missing walk.

```saffron
fun f() {
    var base: Int = 10
    var g: Fun = fun (x: Int): Int => x + base   // warns for 'base'
    return g(1)
}
```

`infer_type`'s `Lambda(p, r, b)` arm returns a `FuncType` without ever visiting `b`,
so a variable captured *only* inside a lambda body is never marked used. Reproduces
identically before and after the #121 fix and independently of callee position —
#121 walked the callee of a `Call`; this is a different node's un-walked subtree.

The fix wants its own test: walking a lambda body means pushing the lambda's
parameter scope first, or the parameters themselves read as unused. Noted in a
comment in `test/pass/callee_position_is_a_use.sf`.

Found while fixing #121.

**Fixed (2026-08-02)** by `check_lambda_body` in `src/compiler/checker.sf`, called
from `infer_type`'s `Lambda` arm. It pushes a scope, defines the lambda's parameters
into it, walks the statements, pops.

Two deliberate non-behaviours. It does **not** run `check_unused_in_scope()` on the
lambda scope, so an unused lambda *parameter* stays unwarned: a callback that ignores
an argument is ordinary, and the parser synthesises Lambdas the user never wrote
(for-in desugaring, trailing closures) whose parameters would draw warnings naming
code that is not in the source. And it clears `current_func_ret` for the walk, because
`parse_lambda` defaults an unannotated lambda's return type to the *string* `"Int"` —
leaving it set would turn `fun () => nil` into a hard error, and a walk that exists to
record uses must not start enforcing a return type nobody wrote.

Diagnostics-only, verified over 378 files: emitted `.ll` byte-identical for all 282
that emit IR, zero exit-code changes. 13 files differ in diagnostics — 16 warnings
removed, every one a capture-only variable (including the `n` this entry predicted in
`test/pass/callee_position_is_a_use.sf`), and one added: `i: cannot infer type` for a
`for-in` inside a lambda, which is pre-existing behaviour the walk merely reaches —
both compilers emit it for the same `for-in` outside a lambda.

`test/pass/unused_var_lambda_capture.sf` shells out to `build/saffronc` because
`run_tests.sh` strips `[checker] Warning` lines. Against the pre-fix compiler **4 of
its 10 assertions fail**, so it detects the defect rather than passing alongside the
fix. Its must-still-warn half includes the case that separates a *wrong* body walk
from an absent one: a lambda parameter shadowing an untouched local of the same name
must still warn.

Caveat worth keeping: the one added diagnostic shows the walk reaches code that was
never type-checked at all before. In a 378-file corpus the single instance was benign,
but user code with heavier lambda bodies may surface more previously-unreachable
diagnostics. Those would be real findings, not regressions — but they will look like
new noise.

---

### 127. FIXED (consequence only) — `src/compiler/checker.sf` did not parse as a standalone module

**Severity: medium** — it makes the checker's public stdlib API (`@check`, `@lang`)
uncompilable and untestable, and it is invisible to bootstrap.

`checker.sf` fails to parse on its own with `expected a literal, name or '(' here`,
pointing at the `and` in a match-arm block body shaped like:

```saffron
if (cond) { false } else { A and B }
```

Consequences: `src/lib/check.sf`, `src/lib/lang.sf`, and any direct
`import "../compiler/checker.sf"` all fail to compile, so the checker cannot be
exercised in-process by a test. Invisible to bootstrap because bootstrap compiles
`checker.sf` only as an import of `_main.sf`, where the surrounding context makes it
parse fine.

Reduced attempts of the same one-liner in a standalone file did *not* reproduce, so
the trigger is narrower than the shape above suggests and needs isolation before a
fix. Blocks writing an in-process `@check` test for #121 (which currently has to
shell out and string-match stderr instead).

Found while trying to write a better test for #121.

**The filed shape above was a red herring, and the reduction that "did not reproduce"
was reducing along the wrong axis.** The trigger needs no `match`, no `if`, and no
method call — it is a STATEMENT beginning with `this` followed by an infix operator:

```saffron
class C { var f: Float
          fun s1(): Float { this.f + 1   return 0 } }   // same error
```

`parse_stmt` routes a `this`-initial statement to `parse_this_stmt`, which consumes the
member/call chain and returns an `ExprStmt` **without handing the result back to the
binary-operator layer**, so the operator is left trying to start a fresh statement. A
block expression's value slot is a statement, which is how an ordinary-looking `and`
landed in that position. Reproduces identically on gen2 and gen3 — not a regression.
The cause is now **#129**.

**Fixed (2026-08-02) at the consequence, not the cause.** `stmts_diverge`
(`src/compiler/checker.sf:3557`) hoists its two recursive calls into locals, dodging the
construct. `stmts_diverge` is a pure AST read, so losing `and`'s short-circuit is a cost
difference only. **The parser defect is untouched and the trap stays set** for the next
author who writes `this.foo() and this.bar()` in a value position.

Two corrections to this entry as filed, both measured. `src/lib/check.sf` **compiled
fine before the fix** (exit 0, byte-identical IR either side) — what was blocked was
*importing* `@check` from a program, which now works;
`test/pass/check_module_imports.sf` asserts through the in-process API, including the
#126 case #121 wanted and could not write, and fails pre-fix at checker.sf's parse
error. Separately, two **pre-existing** checker type errors are newly *visible* on the
standalone path (`strip_nil_from_node`'s if-expression branches typing as
`AST.Type vs NilType`); verified pre-existing by applying the hoist alone to main's
`checker.sf`, which produces them with no #126 walk present. Not fixed — that is
**#130**.

---

### 128. There is no compound-assignment or increment operator, and `test/pass/increment.sf` tests a feature that does not exist

**Severity: low as a defect, medium as misinformation** — the language reads as if
it has these, one test asserts it does, and the failure they produce is a bare parse
error naming no missing feature.

```
x += 1      // [line 2, col 6] Error: expected a literal, name or '(' here
b.v += 1    // same
a++         // same
```

The lexer has no token for any of them. `TokenKind` (`src/compiler/lexer.sf:3`-77)
lists `TkPlus`, `TkMinus`, `TkStar`, `TkSlash`, `TkPercent`, `TkEq`, `TkEqEq`,
`TkBangEq`, `TkLtEq`, `TkGtEq` — and nothing for `+=`, `-=`, `*=`, `/=`, `%=`, `++`
or `--`. So this is not a parser gap over a lexed token: the characters lex as two
separate operators and the parser then wants an operand where `=` sits.

`test/pass/increment.sf` asserts otherwise — its first line is `a = a++;` — and it
is in the 24-name failure baseline as `compile-error`, red long enough to be treated
as scenery. It is a test for a feature that was never implemented, not a stale
spelling of one that was. Deleting it and adding the operators are both defensible;
leaving it as an unexplained red is not.

Found while checking whether the compound forms of #122 reached that bug's
fall-through. They do not — they never get past the lexer — which is why the #122
fix neither covered nor needed to cover them.

---

### 129. A statement beginning with `this` swallows a following infix operator

**Severity: medium** — a parse error on valid-looking code, and the message points at
the operator rather than the cause, so it reads as "the `and` is wrong" when the
problem is that `this.f` already ended the statement.

```saffron
class C { var f: Float
          fun init() { this.f = 1 }
          fun s1(): Float { this.f + 1   return 0 } }
// [line 4, col 32] Error: expected a literal, name or '(' here
```

`parse_stmt` routes a `this`-initial statement to `parse_this_stmt`, which consumes the
member/call chain and returns an `ExprStmt` directly, never handing the result back to
the binary-operator layer. Any infix operator that follows is left trying to start a
fresh statement. A block expression's value slot is a statement, so the natural
spelling `{ this.foo() and this.bar() }` hits it — that is what kept `checker.sf` from
parsing standalone (#127), which was worked around in `checker.sf` rather than fixed
here. Reproduces on gen2 and gen3 alike.

Two things measured directly, which bound the bug and were not assumed:

- **It is specific to `this`.** The identical shape through a local variable
  (`var o = this   o.f + 1`) compiles clean, so the defect is in `parse_this_stmt`'s
  terminal return and not in statement-versus-expression parsing generally.
- **`super.` is NOT affected.** `super.g() and super.g()` in a value position compiles
  clean. So the fix is one function, not a family of them — worth stating because the
  obvious guess is that every keyword-initial statement shares the shape.

The fix presumably wants `parse_this_stmt`'s result to re-enter the expression parser at
the binary-operator precedence level rather than returning an `ExprStmt` terminally.
Worth checking the sibling terminal returns in that function (the `IndexSet` and
`SetField` early returns) the same way.

Found while fixing #127, whose filed reduction had been searching along the wrong axis.

---

### 130. Two `strip_nil_from_node` branches type as `AST.Type` vs `NilType`

**Severity: low** — pre-existing, and invisible unless `checker.sf` is compiled
standalone, which only became possible with #127's fix.

`strip_nil_from_node`'s if-expression branches type as `AST.Type` in one arm and
`NilType` in the other, which the checker rejects on the standalone path. Confirmed
**pre-existing rather than introduced** by applying #127's hoist alone to main's
`checker.sf`, with no #126 lambda-body walk present: both errors appear. So they are
neither #126's nor #127's doing — they were simply unreachable while #127 stopped the
file from parsing at all.

Filed rather than fixed because it was outside the owning agent's scope. Note the
shape: an incomplete type on a branch is exactly what "complete types are a
requirement" is meant to catch, so this is a soundness gap and not only a nuisance.

---

### 131. wasm64's identity discipline makes every non-String value unprintable

**Severity: high**, and the remaining half of #125 — wasm64 output is no longer
*entirely* dropped, but only strings survive.

`src/runtime/values.spec:41-44` gives wasm64 `discipline=identity` (raw i64 bits) and
wasm32 `discipline=nanbox`. Under identity there is no tag to discriminate on, so
`__io_println_any` in `wasm_base.ll` can only treat its argument as a string pointer.

Measured with `scratch/w64/battery.sf` across all three targets. native and wasm32 both
print all six lines (`a string / 42 / true / false / 3 / hello world`); wasm64 prints
the two strings and **four blank lines** for `42`, `true`, `false` and `l.length()`.

Not a local patch. It needs either switching `wasm_base.ll` to the nanbox discipline
wholesale — which also means adding the GC object headers nanbox tagging assumes — or
giving identity-mode values a type-carrying header. Until then, **treat wasm64 as
string-output-only.**

Related and worth reading together: this is the same trap as #125's wrong premise. An
identity i64 bitcast to double is a denormal, and `fptosi` truncates every denormal to
0, so a nanbox helper dropped into an identity base fails silently rather than loudly.

---

### 132. wasm64's link line accepts every undefined symbol, and nothing in the tree tests wasm64 at all

**Severity: high as a process defect** — this is the mechanism that made #110, #124,
#125 and four further missing symbols silent instead of link errors. Filed as its own
entry because fixing the individual symbols does not stop the next one.

Two independent halves, both verified in the source:

**The flag divergence.** `tools/saffron:357` links wasm64 with
`-Wl,--allow-undefined`, while `tools/saffron:412` deliberately links wasm32 with
`-Wl,--import-undefined`, carrying a comment that `--allow-undefined` "silently accepts
EVERY missing symbol." `tools/build_wasm.sh:77` also passes `--allow-undefined`. So on
wasm64 an undefined symbol becomes a no-op host import: the module builds, runs, and
does nothing. Also noted while measuring: wasm32 passes `--identity-mode` for the
runtime compile (`tools/saffron:384`) and wasm64 (`:352`) does not.

Sweeping a linked wasm64 module's `*UND*` list — rather than reading `wasm_base.ll` —
found **four more** undefined symbols beyond #124's and #125's: `__string_intern`
(which alone made all string interpolation produce no output), `__print_debug_location`,
`__val_tag_ptr_nullable`, and `__builtin_trap`, the last `declare`d at
`wasm_base.ll:557` but never defined, so a trap was a silent return. All four are now
defined. **The only reliable audit of a wasm64 base's completeness is dumping the
linked module's undefined symbols, not reading the file.**

**The coverage hole.** `tools/run_tests.sh` never builds for wasm64 — its single
`wasm` mention is `NOT_A_TEST="goals hello_wasm gc_generational_test"` — and
`tools/differential.sh` sets `ALL_CONFIGS="native-O0 wasm32"`. No harness in the tree
builds or runs a wasm64 module, which is how six undefined symbols coexisted with a
green suite.

Compounding the two: **linking cannot serve as the check here.** A successful wasm64
build is compatible with the module doing nothing at all, so any wasm64 test must
execute the module and assert on stdout, treating empty output as failure.
`scratch/w64/regress_wasm64.sh` is such a check (3 cases, empty stdout is FAIL),
written to be adoptable by `run_tests.sh` unchanged.

The flag was left as-is deliberately: it may be load-bearing for legitimate host
imports (`js_log_str` is one), so tightening it is its own measured change rather than
a one-line edit.

---

## Resolved

Full narratives for bugs that are closed. Kept in the file rather than deleted
because several of these entries are the only written record of *why* a
subsystem is shaped the way it is, and of the measurement mistakes that let the
bug survive.

### 6. FIXED — `break`/`continue` outside a loop is now a checker error

**Severity: medium** — a soundness gap, not just a nuisance: the checker
accepted a `break`/`continue` with no enclosing loop, and codegen emitted a jump
to a label that was never created.

The checker's `Break`/`Continue` arms in `check_stmt` were empty — the runtime
nodes existed and the checker ignored them (this was the original filing's whole
content: "Runtime works; type checker just ignores them"). So `fun main() {
break }` compiled clean.

Fix: a `loop_depth: Float` counter on `Checker` (`checker.sf`), initialised to 0.
It is incremented around every `While` body and decremented after — and because
the parser desugars all three loop forms (`while`, C-style `for`, `for-in`) to
`While`, that one site covers every loop. `Break`/`Continue` at depth 0 is
reported as `'break'/'continue' outside of a loop`.

The subtle half is the boundary reset: `loop_depth` is saved and set to 0 at the
top of the `FunDecl` arm and `check_lambda_body`, then restored, because a loop
does not carry into a function or lambda declared in its body. So `while (c) {
fun f() { break } }` correctly rejects the `break` — it is lexically inside the
loop but semantically inside a function that has no loop of its own.

Tests: `test/fail/break_outside_loop.sf` (including the function-boundary case)
and `test/fail/continue_outside_loop.sf` must be rejected;
`test/pass/loop_control_valid.sf` exercises every legal placement — each loop
form, `continue`, nested loops where an inner `break` exits only the inner loop,
and a loop inside a function nested in another loop — and its 6 assertions pass.
Red→green was verified against the pre-fix build (it accepted the fail cases,
RC=0; the fixed build rejects them, RC=1). Suite failure set unchanged at the
24-name baseline, zero regressions.

This is stage 2 of `docs/design/compiler-rewrite.md`, which predicted exactly
this shape: "`Break`/`Continue` … the checker cannot 'just ignore them' because
the exhaustive match requires an arm." The exhaustive match already forced an
arm to exist; it was just empty. No new syntax, so gen2 was untouched and no
promotion was needed.

### 134. FIXED — a method call on a concrete builtin receiver dispatched to an unrelated user class

**Severity: high — a silent wrong answer, and a segfault in the field-touching
variant.** A call to a method name the receiver's real type does not have, but
which *some* unrelated user class declared, resolved to that class's method
instead of being rejected.

```saffron
class Widget { fun init() {} fun frobnicate(): Int { return 42 } }
fun main() {
    var s: String = "ab"
    IO.println(s.frobnicate())   // printed 42 — String has no frobnicate()
}
main()
```

`String` has no `frobnicate`, `Widget` was never even constructed, yet the call
printed `42`. With a field-touching body the unrelated class's `this` became a
`String` pointer read at a class field offset — `Segmentation fault: 11`.

**Not String-specific.** The same `Widget`/`frobnicate` shape over five receiver
types (String, Int, `List<Int>`, Bool, Float) all printed `42`, so it was not
"String is under-checked" — a call on *any* concrete builtin receiver, when
nothing legitimately matched, fell through to whatever user class declared that
name. Distinct from #37 (no branch matches → call silently dropped): there the
*wrong* branch matched. Same dispatch-permissiveness family as #37/#38/#91.

The cause: `find_class_for_method(method)` is a heuristic for a receiver whose
type codegen could not resolve, and three sites in `gen_method_call` invoked it
without first checking the receiver's static type. A `String` receiver resolved
to `""`, `is_user_class` stayed false, and the heuristic found `Widget`. Fixed by
`recv_is_concrete_builtin(obj_type)` — true for a primitive, container or
StringBuilder, false for `Any` (which keeps the heuristic and its own
annotate-the-receiver diagnostic) and for user classes — gating all three
`find_class_for_method` sites. A concrete builtin with no matching builtin arm
now reaches the terminal fall-through, which already reported "type 'X' has no
method 'm'". The genuine builtin methods (`length`, `to_upper`, `abs`, `floor`,
…) are dispatched by the arms above and are unaffected.

`test/fail/builtin_receiver_misdispatch.sf` (must be rejected) and the legitimate
builtin methods verified still working. Note the guard is keyed on the *static*
type: a receiver whose type is genuinely `Any` still rides the heuristic, so the
"annotate the value" story for #37 is intact.

### 135. FIXED — a method inherited from a grandparent but not redeclared on the leaf had no dispatch symbol

**Severity: high — rejected valid programs at codegen.** A three-level chain where
the middle class overrode a method and the leaf did not redeclare it failed to
compile *when the call went through a base-typed receiver* (the case that forces
runtime dispatch):

```saffron
class Base { fun init() {} fun label(): String { return "base" }
             fun show(): String { return this.label() } }
class Mid extends Base { fun init() {} fun label(): String { return "mid" } }
class Leaf extends Mid { fun init() {} }
var x: Base = Leaf()
x.label()   // was: [codegen] Error: no symbol for method 'label' on class 'Leaf'
```

`var x: Leaf = Leaf(); x.label()` compiled and printed `mid` — the failure needed
the base-typed variable, which makes codegen emit a runtime dispatch arm per
concrete descendant. In `gen_virtual_dispatch` each arm switches on the
descendant's runtime tag but derived the *called symbol* from the descendant's
own name (`sc`), so for `Leaf` it asked `resolve_method_symbol` for `Leaf__label`
— a forwarder never emitted, because `Leaf` inherits `label` from `Mid` without
redeclaring it. The arm already computed `sc_owner` (`effective_method_owner(sc,
method)` = `Mid`, the class that actually declares the method); the fix derives
the symbol's bare name from `sc_owner` while still switching on `sc`'s tag. When
the descendant does redeclare, `sc_owner == sc` and behaviour is unchanged.

`test/pass/grandchild_dispatch.sf`, 7 assertions: leaf through a base slot,
through an inherited `show()`, a redeclaring `Mid` (the always-worked arm), the
base itself, a `List<Base>`, and a four-level chain where the resolved owner is
two hops up (a one-level walk would still miss it).

### 136. FIXED — a semicolon inside a block EXPRESSION was a parse error

```saffron
var r = { var q = 5; q }   // Error: expected a literal, name or '(' here
```

The parser desugars a block expression `{ stmts...; value }` via a loop at
`parser.sf:930` that, unlike every other statement loop in the file, never
drained the `;` separator before calling `parse_stmt`, so the bare `;` was handed
to `parse_stmt`. `{ 1; 2 }`, `{ 7; }` and `{ ; 7 }` all failed the same way. The
multi-line form worked only because newlines are not tokens — which is exactly
what hid it, since a multi-statement block is naturally written across lines, and
semicolons work fine in a `fun` body (`parse_block_stmts` at `parser.sf:3213`
already had the skip).

Surfaced by the abb4fbf branch evaluation, which bisected the *visible* error to
`3138807` (the #76 checker-recursion fix): that commit added the
`_ => { this.parse_error(...) }` arm to the primary-expression match and so
turned this pre-existing silent-garbage path into a hard error. The missing `;`
skip itself is older. Distinct from #129 (a `this`-initial statement swallowing
an infix operator — no `this` here) and #128.

Fixed by mirroring `parse_block_stmts`: drain `;` at the top of the loop and
re-check `}`/eof after the drain so a trailing `;` closes the block (yielding nil)
rather than demanding another statement. Regression test
`test/pass/block_expr_semicolon.sf`, 6 assertions covering single/multiple/
leading/trailing semicolons, two values, and the still-working multi-line form.

### 133. FIXED — a variable used only inside an `and`/`or` operand was reported unused

```saffron
fun f() {
    var e: Int = 1
    if (e > 0 and e < 5) { IO.println("ok") }   // Warning: unused variable 'e'
}
```

`infer_type`'s `Logical(left, op, right)` arm (`checker.sf:2632`) returned
`AST.Type.BoolType` without visiting either operand, so both subtrees were
invisible to the used-variable bookkeeping. Fixed by walking them; the arm still
returns `BoolType` unconditionally, because it is a walk and not new inference.

Reproduced against the untouched checked-in gen2, so it predates the current
round of work and is not a regression from it. Cosmetic — a false diagnostic, no
wrong code emitted.

**The axis was measured, not guessed.** Eight shapes were compiled and the
warnings counted one at a time:

| shape | warns |
|---|---|
| `if (x == 1 and true)` | yes |
| `if (true and x == 1)` | yes |
| `if (x == 1 or true)` | yes |
| `var b: Bool = x == 1 and true` | yes |
| `IO.println("${x == 1 and true}")` | yes |
| `if (x + 1 == 2 and true)` | yes |
| `if (!(x == 1))` | **no** |
| `if ((x == 1) == true)` | **no** |

So it was never specific to `if` conditions, and `!` (Unary) and a nested `==`
(Binary) were already correct — both of those arms recurse. That table is what
located the single arm; reading the file would have offered `apply_narrowing`'s
two `Logical` arms as equally plausible culprits, and they are not the bug.

**This is the third instance of one defect class, and naming it predicts the
fourth.** #121 (the callee of a `Call`), #126 (a lambda's body) and #133 (a
logical operand) are all `infer_type` arms that knew their own result type and
therefore never asked what was underneath them. `Logical` is the easiest of the
three to write wrongly, because unlike `Binary` the operand types genuinely *do
not* contribute to the result — Saffron's `and`/`or` yield a `Bool` rather than
one of their operands — so returning `BoolType` without recursing looks complete
rather than truncated. **The audit this suggests:** any `infer_type` arm whose
body is a bare type constructor with no `infer_type` call is a candidate. At the
time of writing `GetField(obj, field) => AST.Type.AnyType` and
`ListLit(elements) => AST.Type.GenericType("List", [AnyType])` both have that
shape.

Regression test: `test/pass/unused_var_logical_operand.sf`, 16 assertions,
modelled on `unused_var_lambda_capture.sf` (#126) and
`unused_var_callee_position.sf` (#121) — it shells out to `build/saffronc` and
greps the diagnostics, because `run_tests.sh`'s `filter_noise()` strips every
`[checker] Warning` line from a test's captured output, so an in-file "no
warning" test would pass no matter what. Measured 6/16 before the fix and 16/16
after. It asserts both halves: left operand and right operand separately (two
recursive calls, so dropping either is a one-line half-fix), `or` as well as
`and` (a fix keyed on `op == "and"` — which is how `apply_narrowing` is
legitimately written a few hundred lines above — would leave `or` broken), a use
reached *through* arithmetic, and three must-still-warn cases so the walk cannot
become a blanket suppression.

One wrinkle worth recording because it cost a cycle: the interpolation case
cannot be written as a string literal. A `${...}` inside this test's own source
is interpolated by the compiler *building the test*, where `s` names nothing, so
the literal has to be assembled by concatenation around a helper returning `"$"`.

---

### 116. FIXED — enum payload variants compared by address, so two equal values were unequal

```saffron
enum Color { Red, Green }               // no payload variant anywhere
enum Option { Some(value: Int), None }  // has one

Color.Red == Color.Red              // true  — both are the immediate `tag << 56`
Option.Some(42) == Option.Some(42)  // false — two distinct __sf_malloc's
Option.Some(42) == Option.Some(43)  // false — correct, but for the wrong reason
Option.None == Option.None          // false — and None carries no payload at all
```

Two things about the shape of this, worth keeping because they are what made the
bug survive so long:

- **The third line answered correctly by accident.** A test that only checks
  *unequal* values passes while equality is entirely broken. That is why the
  regression test asserts invariants rather than a list of expected booleans.
- **`x == x` on one variable answered true**, because both operands load the same
  address. A repro must construct the value **twice**.

**The axis was the enum *declaration*, not the variant.** `Option.None` compared
by address even though it has no fields: once any variant of the enum carries a
payload, `gen_enum_construct` heap-allocates *every* variant, so a fieldless one
becomes a one-slot `[tag]` array instead of the immediate. Adding a field to one
variant therefore silently changed `==` for every *other* variant of the same
enum. `Color.Red == Color.Red` was true only because `Color` declares no payload
variant anywhere. Stated plainly, this was a language-semantics wart and not just
a bug: `==` on an enum meant **different things** depending on whether the
declaration contained a payload variant — value equality if not, identity if so —
chosen by a detail of the declaration the comparison site cannot see.

**Which site was live, and how that was established.** The entry above originally
blamed `expr_body.sf:958`. That was right, but only because it was checked: there
are *two* plausible arms, the `==`/`!=` block and a general comparison block ~300
lines below it, and reading them does not tell you which one an enum `==` reaches.
Marker strings settled it — `; MARKER-SITE-A-EQ-BLOCK` and
`; MARKER-SITE-B-CMP-BLOCK` were inserted, the compiler rebootstrapped, and the
emitted IR read back: for a file with three enum `==` comparisons, A appeared 3/3
times and B never. **This is the third entry in a row (#118, #122, #116) where
the readable-looking arm was a candidate and only a marker sweep plus a
rebootstrap could say which fires.** Reasoning from the source is not evidence
here.

**The fix: a generated per-enum `__enum_eq_<Name>`.** The prerequisite chain this
entry previously laid out — give `gen_enum_construct` a GC header so
`__rt_gc_tag_of` can identify the value, then route `==` to a runtime helper — was
**not implementable as specified**, and finding out why is the useful part. The
heap array is a *bare* pointer: no NaN-box tag, no GC header, and variant arity
and field types exist **only** in codegen's `enum_variant_fields`. A runtime
helper has nothing to read. But everything it would need is known *statically* at
the comparison site, so the comparison became a generated function instead:

- Emitted once per enum per module, registered in `defined_funcs` **before** its
  body is built, which is what makes a recursive enum
  (`enum Tree { Leaf, Node(l: Tree, r: Tree) }`) emit a self-call rather than
  expand forever.
- Body shape: bitwise-identical fast path → a guard that both operands really are
  bare heap pointers (top 16 bits zero; anything else, e.g. a value arriving
  through `Any`, answers with the bitwise result rather than dereferencing tag
  bits) → null checks → **tag equality, then payload**, in that order, so a
  fieldless variant cannot let `None == Some(0)` answer true → a switch to a
  per-variant arm comparing that variant's fields in declaration order,
  short-circuiting to unequal, so a difference in the *first* field and in the
  *last* are both detected.
- A fieldless variant's arm is "the tags matched", which is what makes
  `Option.None == Option.None` true.
- `!=` is the negation of the same call, so the two operators cannot drift apart.
- Per-field lowering is chosen by the field's **declared** type — which is the
  whole reason this belongs in codegen, since the slot itself is an opaque i64
  that says nothing about which comparison is right. String → `__string_eq`;
  Float/Number → `fcmp oeq` on doubles, so `-0.0 == 0.0` as elsewhere in the
  language; a nested heap-encoded enum → recursion, so equality is deep over the
  whole enum tree; Int/Bool/Nil and reference types (class, List, Map, Set) → bit
  equality, the latter deliberately, because value equality for a mutable
  reference is a separate design question enum equality should not decide alone;
  an unresolvable type (an `Any` field, or a generic parameter `T` that names
  nothing at IR level) → `__any_eq`, the same delegation #104 made for ordering.
- The body is emitted into a scratch `StringBuilder` and parked in `globals`, the
  same save/restore a nested lambda uses. That is not incidental: routing through
  the normal pointer and untag helpers is what makes wasm32's i32 pointers work
  here for free, and the save/restore has to nest because the recursive case
  re-enters the emitter mid-body.
- Identity mode is guarded separately: it has no tags to interpret, and it links
  against `base.ll`, which **has no `__any_eq` at all** — so the fallback simply
  is not available there and every non-String field is compared as its bits.

`declared_field_type` was added rather than reusing `get_variant_field_type`
because the latter collapses its answer onto the handful of names codegen lowers
with, and equality needs to tell a nested enum from a class — exactly the
distinction that collapse discards.

The general lesson: **when a fix's stated prerequisites turn out to be
unbuildable, that is information about where the knowledge actually lives, not a
reason to build them anyway.** The type information had never left codegen; the
chain assumed it should be pushed into the runtime, and the shorter fix was to
generate the comparison where the knowledge already was.

Regression tests: `test/pass/enum_payload_value_equality.sf` (25 assertions, was
16/25, now 25/25) and `test/pass/enum_eq_payload_kinds.sf` (33 assertions, added
with the fix, covering String / Float / nested-enum / reference / `Any` payload
kinds). The `KNOWN_FAIL` entry was removed in the same commit as the fix, so an
`xpass` cannot go unnoticed. No gen2 promotion needed — the fix changes what the
compiler *emits*, not what it can parse.

---

### 119. FIXED — an output path containing `.sf` was taken as the input, so the real input was never read

**Severity: high**, and higher than it looks: it makes a compiler that *rejects* a
file appear to accept it, which corrupts measurement rather than just output.

```
saffronc real.sf decoy.sf     # exit 0 — compiles decoy.sf; real.sf never read
saffronc real.sf out.sf.ll    # exit 0 — writes out.sf.ll.ll
```

Verified at HEAD, both cases: after `saffronc /tmp/real.sf /tmp/decoy.sf`, the
emitted IR contains `DECOY` once and `REAL_INPUT` **zero** times, with exit 0.

`main.sf:1075` picks the input by scanning for `arg.contains(".sf")` and keeping the
**last** match:

```saffron
if (arg.contains(".sf") and arg != "--stdlib") {
    last_sf_idx = i
}
```

Two independent defects in one line. `contains` rather than `ends_with` matches
`.sf` anywhere in the string, so `out.sf.ll` qualifies; and taking the *last* match
means when both args qualify the **output** wins. Note the rest of `main.sf`
already knows better — lines 693, 714, 742 and 968 all use `ends_with(".sf")`. Only
the argument parser sniffs.

The fix is `ends_with(".sf")` plus treating the input as *positional* (first
non-flag argument) rather than sniffing for it at all. Sniffing cannot distinguish
an input from an output that merely resembles one; position can.

**Why this matters beyond the obvious.** The failure is silent and exit code 0, so
any batch sweep whose output filenames embed `.sf` measures nothing while appearing
to measure everything. This was found by an agent whose first two arity sweeps
wrote to `$OUT/<name>.sf.ll` and reported zero diagnostics across the corpus —
including on a file it had just watched the compiler reject. The result looked like
strong evidence of no regression and was evidence of nothing.

The shipped pipeline is **not** affected, which is why this survived: `tools/saffron`
writes `$TMPDIR/output.ll` (`tools/saffron:320,441`) and `tools/run_tests.sh` writes
`neg_<name>.ll`, neither containing `.sf`. So the test suites and every failure-set
comparison made through them remain valid. It is ad-hoc sweeps that are at risk.

**FIXED (2026-08-02).** The input is now **positional** (`main.sf:1070`): the first
non-flag argument is the input, the second is the output. `ends_with(".sf")`
deliberately appears nowhere in that decision. Switching `contains` to `ends_with`
would *not* have fixed this — an output path is allowed to resemble an input path,
so no test on the name can separate the two. Only position can.

Flag values are skipped so they cannot be promoted to the input: `--stdlib`,
`--lib-path` and `--target` each consume the argument after them, which is why
`--lib-path vendor.sf` does not become the input. That list mirrors the flag loop
immediately above; every other flag is a bare switch.

One subtlety the fix depends on, worth recording because the two loops now look
inconsistent: the positional loop starts at `i = 1` while the flag loop above starts
at `i = 0`. That is correct, and necessary — `OS.args()[0]` is the binary's own
path, which does not start with `-` and would otherwise be taken as the input.
Verified by probe rather than assumed.

Two further silent-success paths in the same function went with it, both the
identical shape and each independently enough to defeat a sweep:

* **No input at all exited 0.** It printed the usage text and returned 1 — but
  `return 1` from `main()` does not set the process status, only `c_exit` does, so
  the shell saw success. Now exits 1.
* **A nonexistent input exited 0 with valid IR.** `IO.read_file` on a missing path
  returns `""`, which lexes and parses as an empty program, so `saffronc missing.sf
  out.ll` emitted a 6.5 KB `.ll` and exited 0. A typo'd path in a sweep read as a
  clean compile. Now `saffronc: cannot open input file: <path>`, exit 1, and no
  output file is written.

These were folded into #119 rather than filed separately: same function, same
silent-success mechanism, and the analysis above already frames the bug as being
about measurement rather than output.

**`test/pass/cli_positional_input.sf` is the regression test, and the negative
evidence is the part that matters.** Argument parsing has no in-language surface, so
the test drives the real `build/saffronc` binary. Run against a pre-fix compiler
staged into a throwaway root, **exactly 9 of its 26 assertions fail** — naming the
first-positional rule, the doubled `.ll`, the `--lib-path`-value case, and both exit
codes. 26/26 against the fixed compiler. So it detects the defect rather than merely
passing alongside the fix, which is the distinction #107 exists to enforce.

It locates the repo root by walking up for a directory holding **both**
`build/saffronc` and `src/lib/prelude.sf`, and **fails rather than skips** when it
finds none. A guard test that quietly passes without running is the exact failure
mode this bug is about. An earlier draft reconstructed the root by slicing a fixed
character count off `<root>/build/saffronc` and was off by one, silently yielding a
neighbouring directory; the two-marker walk has no magic constant to get wrong.

Every invocation form in the shipped scripts was enumerated and verified —
`tools/saffron` lines 225/328/352/384, `tools/run_tests.sh:375`, `bootstrap.sh`
115/164/235/279/302/335, and `test/package_map_test.sh` (18 passed, 0 failed).
`tools/saffron` never forwards extra positionals to `saffronc`; it always passes
exactly input + output, so nothing depended on the old last-match behaviour.

---

### 118. FIXED — a nonexistent module member compiled clean, then emitted invalid IR blaming the compiler

**Severity: high.** Not a wrong answer — a compiler crash *reported as a compiler
bug*, blaming itself for a plain typo in the user's program.

```saffron
import "@math" as Math
IO.println(Math.NOPE)     // compiler exits 0; the IR is then rejected
IO.println(Math.pi)       // 3.14159 — a real member is fine
```

```
saffron: the compiler emitted invalid LLVM IR for test/pass/math.sf
saffron: this is a compiler bug, not an error in your program.
  opt: output.ll:1288:24: error: use of undefined value '%Math'
    %t1 = load i64, i64* %Math
```

The last line is the whole story: having failed to resolve the *member*, codegen
falls back to evaluating the **namespace itself** as if it were a local variable,
and emits a load from an SSA name that was never defined. The message is actively
misleading — it tells the user their correct compiler is broken when in fact their
program has a typo, so the diagnostic points at the wrong party.

Mechanism, and it is the recurring pattern again (**eighth instance**, after #22,
#40, #78, #37, #103, #113, #114): a resolver with no way to say *not found*.
`expr_body.sf:311` handles a module-prefixed member by trying two lookups —
`known_functions` for `Module.fun`, then `module_globals` for `Module.global`. When
both miss it does not report absence; it simply falls out of the block, and control
reaches the generic receiver path at `expr_body.sf:332`, which evaluates `object`
— the bare alias `Math`. The undefined-variable check that would have caught that
(`expr_body.sf:91`) explicitly exempts `module_prefixes.has(name)`, and rightly so
for a *valid* member access, so nothing else stops it.

The fix belongs in the `module_prefixes` block: after both lookups miss, report
`no member 'NOPE' in module 'Math'` and set `has_errors`, rather than falling
through to a path that can only produce a load from a namespace. The alias is known
to be a module at that point, so "not found" is expressible there and nowhere
downstream.

Found while checking a claim that `test/pass/math.sf` was merely a stale test
calling `Math.PI` when `src/lib/math.sf:21` defines lowercase `pi`. The test *is*
wrong, but that is not why it fails: any nonexistent member reproduces this, so
renaming the constant would hide the bug rather than fix it. `test/pass/math.sf`
remains red pending the diagnostic; fixing the test alone would be papering over
the real defect.

**FIXED (2026-08-02).** The fix reports absence where absence is expressible: after
both lookups miss, `[codegen] Error: no member '<member>' in module '<alias>'` plus
`has_errors`. The alias is *known to be a module* at that point and nowhere
downstream, which is why the diagnostic belongs there rather than in the generic
receiver path. Verified: `Math.NOPE` went from no output / exit 0 / `%t1 = load i64,
i64* %Math` in the emitted IR, to the named diagnostic and exit 1. `Math.pi` is
unaffected, exit 0 before and after.

Eighth instance of the can't-express-unknown family (#22, #40, #78, #37, #103,
#113, #114): a resolver with no way to say "not found" guesses instead of reporting
absence.

**The block exists in two copies, and the one that fires is not the one the
original trace named.** This entry first pointed at the `MemberAccess` arm
(`expr_body.sf:311`). That arm is **dead**: `gen_arg_value`'s parallel copy of the
same block (`:1843`) wins for every spelling. Established rather than argued — the
two arms were given textually distinct messages, the compiler rebootstrapped, and
14 syntactic positions swept: bare `var`, call argument, operand of `+`, `==`
condition, list element, inside a function body, bare statement position, chained
`.length()`, map value, string interpolation, lambda body, `for-in` subject, and
index base. The `gen_arg_value` copy fired in every position that fires at all; the
`MemberAccess` copy fired in none. Both were hardened anyway, the dead one carrying
a comment recording that it is currently unreachable.

That two live copies of one resolution block exist at all is the more durable
lesson: a fix applied to the readable one would have changed nothing, bootstrapped
green, and passed a hand-written test only if the test happened to use a spelling
that reaches it.

**Completeness was measured, not assumed.** A probe compiler logged every arrival
at the miss point across 336 files (`test/`, `test/pass/`, `test/fail/`, `src/lib/`,
`src/compiler/`, `src/runtime/`, `examples/`, `turmeric/`, `basil/`, `bazaar/`,
`parsley/`, `pantry/`) — exactly three arrivals, and `opt` rejects the IR for all
three, so no valid spelling depended on the fall-through. Each of these was then
confirmed by direct fixture to resolve *before* the block: module globals, module
functions called and used as values, zero-arg and argument-bearing enum variants
through an alias, class **and actor** constructors through an alias, `@inline`
functions, forward-referenced module globals read inside a function, enum/class
types aliased through an alias (`var A = Inner.Color`, lowered as a `ptrtoint`
wrapper), locals shadowing a module alias, named imports, and the builtin
`IO`/`GC`/`Reflect` namespaces.

Tests: `test/fail/module_member_missing.sf` and `test/pass/module_member_valid.sf`
(9 assertions over the valid spellings). Per #107 discipline, no `.expected` file
freezes the wrong output.

`test/pass/math.sf` is **left wrong on purpose**, as evidence. It calls `Math.PI`
while `src/lib/math.sf:21` declares lowercase `pi`. It was reported as merely a
stale test; it is stale, but that is not why it was failing, and renaming the
constant would have hidden this bug. It now fails as `compile-error` with the clean
diagnostic instead of `invalid-ir` blaming the compiler.

**Not fixed here, filed as #122:** *assignment* to a nonexistent member
(`Math.NOPE = 3`) still exits 0 and still emits `load i64, i64* %Math`. The
assignment path never reaches the member-read block, so it kept the original #118
signature — verified identical before and after this fix.

**Also found while measuring, filed as #123:** a module-alias collision.
`src/lib/ast.sf:13`, `src/lib/lexer.sf:9` and `src/lib/parser.sf:10` each bind a
*different* module to the alias `Internal`, and `src/lib/lang.sf` imports all
three. `src/lib/lang.sf` moves from exit 0 to exit 1 under this fix, which is not a
regression: it was **already** emitting invalid IR (`use of undefined value
'%Internal'`, verified on the pre-change build) and now fails with a named
diagnostic instead. `@lang` has no importers in the repo, so no test moves.

---

### 109. FIXED — `is String` / `is List` / `is Map` were unconditionally false on wasm32

`__val_type_id` on wasm32 read the type id from `user+0`, which is the object's
first *payload* word — for a list, its element count. Every wasm32 allocator
stores the tag at `raw+0` and the magic at `raw+8` and returns `user = raw+16`,
so the tag is at `user-16` and the magic at `user-8`: exactly where native puts
them, and exactly where wasm32's own `__gc_get_type_tag` already looked. This
reader was the one site not updated when the wasm32 GC header gained its tag and
magic words — the work recorded as #39, which is listed as fixed but evidently
did not reach every reader of that header.

`__val_is_list` and `__val_is_map` had drifted separately — they still carried
pre-`88297ca` identity-mode bodies — *and* routed through the broken reader, so
one change fixed all three. Verified by probe: `String/List/Int` natively versus
`other/other/Int` on wasm32 before, agreeing after.

The fix was three deleted `@override` blocks and no new code, because
`values.spec` (rewrite stage 9) had by then made the shared `@nanbox` body the
default and each override the explicit exception. That is the generator paying
for itself on its first real use: the drift became visible as three overrides
whose `reason =` lines could not be written honestly.

### 104. relational operators on `Any`-typed operands compare untagged garbage — FIXED

**Severity: critical.** Silent wrong answers, and the answer depends on heap
layout, so it changes with optimization level.

`src/compiler/codegen/expr_body.sf:1127` gates the string-comparison path on a
**static** type test:

```
if ((left_type2 == "String" or right_type2 == "String")
    and (op == "<" or op == "<=" or op == ">" or op == ">="))
    ... call i64 @__safe_strcmp ...
```

When both operands are declared `Any` — which is what every generic container
has — the test fails and codegen falls through to the integer path:

```llvm
%t5 = call i64 @__val_untag_int(i64 %t3)
%t6 = call i64 @__val_untag_int(i64 %t4)
%t8 = icmp slt i64 %t5, %t6
```

`__val_untag_int` on a TAG_PTR value does not yield an integer, so this compares
bit patterns of heap pointers. Mechanism M1 (codegen re-infers types and reaches
a different answer than the checker) plus M3 (representation is a convention).

Repro:

```saffron
fun lt_any(a: Any, b: Any): Bool { return a < b }
IO.println(lt_any("apple", "banana"))   // prints false at -O0
var sa: String = "apple"
var sb: String = "banana"
IO.println(sa < sb)                     // prints true — the static path is fine
```

**Scope is wider than the comparison operator.** `rt_list_sort`
(`src/runtime/runtime.sf:1759`) is a bubble sort whose comparison is
`if (a > b)` on two `Int`-declared locals holding NaN-boxed values. For pointers
the comparison is never true, so **`List.sort()` does not sort strings at all**,
at either optimization level, and returns the list untouched with no error:

```saffron
var l = ["cherry", "apple", "banana"]
l.sort()
IO.println("${l}")      // [cherry, apple, banana]
```

The idiomatic way to iterate a map in key order — `var ks = m.keys(); ks.sort()`
— is therefore silently a no-op.

Also affected: `src/lib/sorted_set.sf` (`_bisect` takes `value: Any`),
`src/lib/heap.sf:141,146` (default comparators are
`fun (a: Any, b: Any): Bool => a < b`), and `T.assert_lt`/`T.assert_gt` in
`src/lib/test.sf:77,85`, which take `Any` and so are unreliable on strings — the
assertion library itself. The `assert_lt`/`assert_gt` exposure is latent, as no
test currently calls them on strings.

`src/lib/sorted_map.sf` vs `src/lib/sorted_set.sf` is a controlled experiment for
the mechanism: same binary search over string keys, but `sorted_map` declares
`key: String` and routes through a hand-rolled `_str_cmp` over an `_alphabet`
lookup table, whose comment says it exists "enabling correct `<` ordering". Someone
hit this and worked around it in the library instead of fixing the compiler.
`sorted_map` is right; `sorted_set` is wrong.

**Why it stayed hidden.** `test/test_sorted_collections.sf` reports
`All 76 assertions passed` at -O2 and `74/76 passed, 2 failed` (exit 1) at -O0,
from identical source through an identical compiler. At -O2 the three interned
literals happen to sit at addresses whose order agrees with alphabetical order, so
it passes **by luck**. `run_tests.sh` builds at -O2, so it is green. The suite's
only two `.sort()` calls (`test/lists.sf:19`, `test/test_stdlib.sf:39`) sort
numbers.

Found by `tools/differential.sh`, as the only mismatch on the pure-native
-O2 vs -O0 axis. Regression test: `test/oracle_any_compare.sf` (fails 16/41 at
-O0, 1/41 at -O2; every control passes — the one still failing at -O2 is a
reverse-alphabetical insert added so layout luck cannot rescue it).

Fix sketch: the comparison needs a runtime type dispatch rather than a static
one, when the static types do not settle it — i.e. a generic `__val_lt` helper in
the runtime that inspects the NaN-box tags of both operands, with codegen calling
it whenever either operand's static type is not a known primitive. `rt_list_sort`
should call the same helper.

**FIXED (2026-08-02) — with a runtime type dispatch, because the static types
genuinely do not settle the question.** The gate above was a static test, so two
`Any` operands failed it and fell to the integer path. New `__val_cmp` in
`runtime.sf` (three-way, strcmp convention) inspects the NaN-box tags of *both*
operands: both TAG_PTR compares contents via `rt_strcmp`; anything else compares
through an order-preserving double key that normalizes Int and Float in one
call. Exposed as `__val_lt/le/gt/ge`, which codegen calls whenever the static
types do not settle the comparison — the new arm sits **above** the Float and
integer arms, per the dispatch-preamble ordering hazard.

`rt_list_sort` now uses `__val_cmp(a, b) > 0` rather than `a > b`, so `.sort()`
and `<` cannot disagree about order. That closes the wider half of this entry:
`List.sort()` returned string lists **untouched** at every optimization level, so
the idiomatic `m.keys().sort()` was a silent no-op.

The fast path is preserved and this was measured, not assumed: an all-numeric
program emits **zero** `__val_*` calls and 7 direct `icmp`/`fcmp`. `Int` stays
`icmp slt i64`, `Float` stays `fcmp olt double`, static `String < String` still
uses `__safe_strcmp`.

**No `.ll` base change was required** — the helpers live in `runtime.sf`, which is
compiled for all three targets, and their `declare` is auto-emitted by
`output_body.sf`'s `called_functions` loop. `__rt_float_bits` reuses the existing
`__val_untag_float`, so no new base symbol at all.

Evidence: `test/pass/cmp_any_ordering.sf` (63 assertions, green at both -O0 and
-O2) and `test/oracle_any_compare.sf` (**25/41 → 41/41** at -O0).
`tools/differential.sh test/test_sorted_collections.sf` went from **2 mismatches
to 0**. Confirmed directly against the two compilers: `["cherry","apple",
"banana"].sort()` prints `cherry banana apple` before and `apple banana cherry`
after — while `a < b` on two `Any` strings answers `true` at -O2 *both* ways,
which is precisely the -O2 address accident that kept the suite green.

Two follow-ups this exposed and deliberately did not fix. `sorted_map.sf`'s
`_str_cmp`/`_alphabet` workaround is now redundant — and *worse* than plain `<`,
verified rather than assumed: `_alphabet` is a printable-ASCII table, so
`"café" < "cafz"` is wrong through the table and right through `<`. And `<` on two
Lists still orders by address, because a List reaches `__val_cmp` untagged
(upper == 0) rather than as TAG_PTR, so it takes the numeric path; the right fix
is for the checker to reject the comparison, not for the runtime to invent an
order. A `__rt_is_gc_non_string` guard was added to keep a GC non-string out of
`strcmp`, and its comment says plainly that it currently only covers TAG_PTR
non-strings — a guard against a future tagging change, not a live fix.

---

### 105. `IO.println(enum_value)` prints a reinterpreted bit pattern — FIXED

**Severity: high.** Silent, and currently producing garbage in two tests that
count as passing.

A fieldless enum variant is represented as the immediate `tag << 56`. Codegen's
`IO.println` path passes it straight through with no `to_string()` call and no int
tag:

```llvm
%t2 = shl i64 0, 56
%t3 = call i64 @__io_println(i64 %t2)
```

`__io_println` inspects the NaN-box tag bits, finds none of
TAG_PTR/TAG_INT/TAG_SPEC, correctly concludes the value is an unboxed double, and
formats the raw bits. `0 << 56` prints `0`; `1 << 56` is `0x0100000000000000`, a
subnormal, and prints `7.29112e-304`.

```saffron
enum Color { Red, Green, Blue }
IO.println(Color.Red)          // 0
IO.println(Color.Green)        // 7.29112e-304
IO.println("${Color.Green}")   // Green  — correct
IO.println([Color.Red, Color.Blue])   // [0, 4.77831e-299]
```

Interpolation is correct because the lexer's `"${e}"` desugaring inserts an
explicit `.to_string()`, which emits `call i64 @Color__to_string`. Two code paths
for one value, one right and one wrong: mechanism M5, and the same shape as
BUGS #77 (branching on a bool was right while printing it was wrong). Payload
variants and list elements are broken the same way, so the working path is
specifically the one that goes through an explicit `.to_string()`.

Closely related to but **not** the same as BUGS #102. #102 was inside the
generated `to_string()`; this path never calls `to_string()` at all, which is why
#102's fix made interpolation work while leaving this broken.

`test/pass/enums.sf` and `test/pass/generics.sf` print these subnormals **today**
and both PASS, because they have no assertions and no `.expected` file (see #107).

Regression test: `test/oracle_enum_println.sf` + `.expected`.

**FIXED (2026-08-02) — by routing through the enum's own `to_string`, not by
teaching `__io_println` the encoding.** The live `IO.println`/`IO.print` arm
passed the value straight through, so `__io_println` found no NaN-box tag on the
bare `tag << 56` immediate, took it for an unboxed double, and formatted the raw
bits. Fixing it inside `__io_println` would have put the `tag << 56` encoding in a
*second* place obliged to stay in step with `gen_enum_construct` and
`emit_enum_to_string` — the shape that produced this bug. Instead the arm converts
through the enum's own generated `to_string()`, so there is one path.

This was feasible even though `get_expr_type` reports `"Any"` here:
`gen_enum_construct` sets `last_type = EnumType(name)`, so immediately after
`gen_arg_value` the type *is* in hand.

**Why it stayed hidden:** string interpolation was always correct, because the
lexer inserts an explicit `.to_string()`. So the enum's `to_string` looked
well-tested while direct `println` silently used a parallel route that skipped it.
`test/pass/enums.sf` and `generics.sf` printed these subnormals on **every run**
and passed regardless, because they assert nothing — #107 exactly.

Verified: `Color.Red` / `Color.Green` / `Shape.Circle(5)` / `Shape.Rect(3, 4)`
printed `0`, `7.29112e-304`, `5.21502e-310`, `5.21502e-310` before and `Red`,
`Green`, `Circle(5)`, `Rect(3, 4)` after. Regression test
`test/pass/enum_println_to_string.sf` + `.expected` (6 of 12 lines were wrong
pre-fix) additionally asserts that direct `println` agrees with interpolation
line-for-line, so the two paths cannot drift apart again.

`enums.sf` and `generics.sf` were **not** given `.expected` files while this bug
was open — see #107, which deliberately refused to record their output. That
refusal is what made this fix a clean change rather than a suite-wide break.

**Still open — an enum *inside* a printed collection.** `IO.println([Color.Red])`
prints `[0, 4.77831e-299]`. Elements are formatted by `__rt_elem_to_string` →
`__any_to_string` at *runtime*, where no static type exists, and a payload variant
is allocated with `__sf_malloc` — no GC header — so the sentinel check
`__rt_as_list_ptr` relies on can never identify it. That needs
`src/runtime/runtime.sf` and/or `base_nanbox.ll`, not codegen. Note
`IO.println(xs[0])` is correct; only elements nested inside a printed collection
are affected.

---

### 106. FIXED — a match-arm binding was stored into the enclosing variable's slot

**Severity: high.** One face is loud, the other is silent.

The arm binding is registered in the enclosing scope rather than in an arm scope.

At **module scope**, the outer variable is a global at `@__g_s` but the arm emits
`store i64 %tN, i64* %s` against a local that was never allocated — invalid IR,
`use of undefined value '%s'`, caught by `opt -passes=verify` in `tools/saffron`:

```saffron
var s: String = "bound"
enum E { Sq(side: Int) }
var m = match (E.Sq(4)) { Sq(s) => "got ${s}" }
```

**Inside a function** `%s = alloca i64` does exist, so the store lands in the
outer variable's slot. A `String`-declared variable silently holds a tagged Int;
`IO.println(s)` prints `4`, `s.length()` prints `0`, exit 0, no diagnostic. This
is the dangerous face.

Renaming the binding (`Sq(zz)`) makes it work, confirming a scoping bug rather
than a payload-decoding one. Distinct from BUGS #76 (non-exhaustive match yields
an indeterminate value) and from #97.

This obstructed building the differential oracle itself —
`test/oracle_stringify.sf` had to rename its match bindings to `mr`/`ms`/`mn` to
get around it.

Regression test: `test/oracle_match_shadow.sf` (currently segfaults, exit 139, at
both -O0 and -O2).

---

**FIXED (2026-08-02).** A match arm's bindings are a scope of their own, but
codegen has no scope chain — only one flat `%<name>` slot per name — so an arm
binding was stored straight into the enclosing variable's slot.

`resolve.sf` already had this right: `resolve_arms` pushes a scope per arm and
`declare_pattern` declares each binding `"local"`, so the binding reaches codegen
as `Ref("local", name, "")` and reads from `%<name>`. The arm was scoped in the
AST and codegen was not honouring it. That means the slot for an arm binding is
always the local one, never `@__g_`, which splits the fix in two:

- **At module scope** that local slot was never allocated, because
  `output_body.sf`'s alloca loop skips names that are module globals. `gen_match`
  now emits it itself via `ensure_arm_slots` from the pre-switch block, which
  dominates every arm block. The global is untouched by construction.
- **Inside a function** the outer local and the binding genuinely share one
  alloca, so the arm is bracketed instead: snapshot the slot on entry, restore
  before the terminator — not at the shared end label, where the restore would not
  dominate.

The flat `typed_vars`/`string_vars` side tables are bracketed the same way, since
a binding's type otherwise overwrote the enclosing variable's and left a `String`
local dispatching as an `Int`. Applies to all three lowerings in `gen_match` —
enum switch, class pattern, and unknown-enum fallback.

Bookkeeping uses an `"__armslot:"` key in `current_fn_locals`, the same
namespaced-key convention `stmts_body.sf` uses for `"__nestedfn:"`, so it gets
per-frame lifetime for free and needs no new class field. It is needed because two
matches binding the same colliding name would otherwise emit a duplicate `alloca`
that LLVM rejects.

Regression tests: `test/oracle_match_shadow.sf` (4 assertions; it **segfaulted**,
exit 139, before the fix) and `test/pass/scope_match_arm_binding.sf` (11).
`test/oracle_stringify.sf`'s bindings were renamed back from `mr`/`ms`/`mn` to
their natural `r`/`s`/`n`, where `s` deliberately collides with a module-global
`var s: String` — so that oracle now exercises the fix incidentally and stops
compiling if #106 regresses.

One case is deliberately absent from the test: an inner match nested directly
inside an arm *body*, the stronger rebinding check. A block-bodied arm currently
types as `Nil|String` and the checker rejects returning it as `String` — a
separate limitation. Sequential matches cover the same slot-sharing question
meanwhile.


### 108. FIXED — enum `to_string()` printed heap addresses for List, Map and nested-enum payloads

**Severity: medium-high.** Silent, and nondeterministic between runs of one
binary.

The generated `to_string()` formats its payload by declared type. Int, Float,
String and Bool are handled; List, Map and nested enum fall through to being
printed as raw integers:

```saffron
enum OneList { OL(v: List<Int>) }
IO.println("${OneList.OL([1, 2])}")   // OL(105553124048920) — varies per run
enum OneMap { OM(v: Map<String, Int>) }
IO.println("${OneMap.OM({"a": 1})}")  // OM(105553146069272) — varies per run
enum Inner { A, B }
enum OuterE { OE(v: Inner) }
IO.println("${OuterE.OE(Inner.B)}")   // OE(0) — stable but wrong
```

A String payload is also a heap pointer and prints correctly (`OS(hi)`), so this
is a per-type formatter list with three types missing, not a general inability to
handle heap payloads. Mechanism M5.

Distinct from #102 (which mis-*decoded* a one-field payload; here the payload
decodes fine and is *formatted* wrong) and from #105 (which never calls
`to_string()`).

Found by the differential's nondeterminism gate: `oracle_stringify.sf` failed a
diff against its own second run. Regression test:
`test/oracle_enum_payload_tostring.sf` (6/14 fail). Its last two assertions
require only that stringification be a pure function of the value — one value
twice, and two equal values — so they hold regardless of what the correct
rendering is.

**FIXED (2026-08-02).** `emit_enum_to_string` formats each payload field by
declared type, and the per-type chain had arms for String, Bool and Float/Number
only — List, Map and nested-enum payloads fell through to the integer default and
printed the payload's heap address. That is why the differential oracle's
*nondeterminism* gate caught it: the address varies per run, so the file failed a
diff against its own second run.

Three arms added, reusing formatters that already exist rather than adding new
ones: `__list_to_string`, `__map_to_string`, and for a nested enum that enum's own
generated `<Enum>__to_string`. All three already return a raw `char*` — exactly
what `__sb_append` wants — so no tag/untag step belongs on any of them, which is
what avoided repeating #102's segfault.

`__any_to_string` was deliberately **not** used: it is absent from
`wasm_base.ll`, and a static field type is already in hand at this site, so
routing through it would have bought nothing and cost wasm64. No wasm64 gap is
introduced, and wasm32 differential agreement confirms both container helpers
exist there.

Stringification is now a pure function of the value. Regression tests:
`test/oracle_enum_payload_tostring.sf` (14 assertions, 6 of which failed before)
and `test/pass/enumfmt_payload_to_string.sf` (12), the latter pinning exact
renderings — `WithList([1, 2, 3])`, `WithMap({k: 1})`, `WithInner(I(7))` — rather
than merely asserting "not an address".

Verified byte-identical across native-O2, native-O0 and wasm32 via
`tools/differential.sh`.

### 111. FIXED — the parser discarded every base after the first, so `class Duck extends Flyable, Swimmable, Walkable` was `extends Flyable`

`ClassDecl.parent` was a single `String`. The parser read the first name after
`extends` and threw the rest away:

```
parent = this.expect_ident()
// Support multiple extends (interfaces): class Foo extends A, B, C
while (this.match_kind(",")) {
    this.expect_ident() // skip additional interfaces for now
}
```
`parser.sf:1975-1979`

Multiple inheritance is a documented feature (CLAUDE.md, "Multiple inheritance:
`class Duck extends Flyable, Swimmable, Walkable`") and it did not work at all
past the first base. `test/pass/multi_inherit.sf` and `test/pass/interfaces.sf`
were both **failing on `main`** for this reason, not passing for the wrong reason:

```
[codegen] Error: type 'Duck' has no method 'swim'
[codegen] Error: type 'Document' has no method 'serialize'
```

Three consequences, one per layer. Codegen emitted method forwarders for
`parents[0]` only, hence the errors above. The checker's `class_parents` was
`Map<String, String>`, so `is_subtype_node` walked a single `X__parent` chain and
`d is Swimmable` was statically false, generic constraint satisfaction only ever
looked one level up, and conformance checked the abstract methods of the first
base alone — an unimplemented requirement on base 2 or 3 was accepted silently.
The runtime helper `__class_is_a` walked `__class_parent_tag` one link per step,
and a `switch` arm returns exactly one value, so no depth of walking could reach
base 2: multiple bases are a DAG, not a chain.

**Fix.** `ClassDecl.parent: String` → `parents: List<String>`, arity-preserving
(still six fields, so no #96 exposure) but a different payload shape, and every
`ClassDecl(` match arm across `parser.sf`, `checker.sf`, `resolve.sf`,
`codegen.sf`, five `codegen/*_body.sf`, `src/lib/{ast,formatter,lang}.sf` and
`tools/gen_docs.sf` updated in the same commit.

- Checker: `class_parents` holds the list; `inherits_from` is a breadth-first
  search over the whole DAG with a visited set (a diamond visits its shared
  ancestor once, a cyclic `extends` terminates) and replaces the chain walk in
  `is_subtype_node` and the one-level check in `type_satisfies_constraint`;
  conformance loops every base.
- Codegen: forwarders are emitted for every base in declaration order, earlier
  bases winning ties as an override already did; `__class_is_a` became a
  flattened tag→ancestor-set nested switch built from a compile-time transitive
  closure (`all_ancestor_structs`), which is also O(1) rather than O(depth);
  `effective_method_owner` and `gen_virtual_dispatch`'s descendant test now
  search all bases.

**Field layout is deliberately unchanged.** Only `parents[0]` contributes fields
and only its `init` is forwarded, because the invariant the whole lowering rests
on — parent field index i == child field index i, every field an i64, which is
what makes `init` forwarding and inherited field access correct with no vtable —
can hold for exactly one base. A single-inheritance class therefore lays out
byte-for-byte as before (no ABI break against the checked-in gen2), and a
secondary base that declares fields gets a diagnostic rather than methods reading
the primary base's slots at its own offsets. Interfaces, the overwhelmingly
common case for `extends A, B, C`, are fieldless and unaffected.
`__class_parent_tag` still reports the primary base; `class_parent_of` keeps its
single-valued meaning and the new `class_parents_of` carries the full set.

Two bugs surfaced only after the widening, both from a bodyless declaration being
treated as a real method once more than one base was visible:

- `test/fail/conformance.sf` stopped being rejected. The child's "what I can
  offer" set gathered inherited names including abstract ones, so
  `class Circle extends Shape` found `Shape.area()` inherited from Shape and let
  the requirement satisfy itself. `inherited_method_names` now subtracts each
  base's `class_abstract_methods`.
- `class Both extends Requires, Provides` where `Requires.compute()` is bodyless
  and `Provides.compute()` concrete compiled cleanly and returned 0 at runtime:
  first-wins forwarding pointed `Both__compute` at `Requires__compute`, which has
  no body. Forwarding now skips a base's abstract names, so the concrete later
  base wins. Conformance also runs in a second pass after all forwarding, so the
  requirement is not reported before the base that satisfies it is visible.

Verified: bootstrap passes both stages including the gen4 fixed point.
`test/pass/multi_inherit.sf` was rewritten to assert rather than print and now
covers all of the above (28 assertions: methods and `is` against bases 1-3, an
override on a non-first base, an abstract satisfied by the class itself, the
abstract-plus-provider pair, a grandchild reaching its grandparent's 2nd and 3rd
base, and a state-carrying primary base combined with fieldless interfaces); it
fails on the base commit with seven `has no method` errors. Full suite: 26
failures, a strict subset of the 28 on `main` — `pass/multi_inherit` and
`pass/interfaces` fixed, nothing new.

This is the prerequisite §1 of `docs/design/access-modifiers.md` names for
`protected`, which it calls "currently unimplementable soundly" precisely because
a `protected` member on `Swimmable` could not be resolved from `Duck`.

### 103. Extension-method resolution returns an unprefixed symbol on a miss, so mutually recursive `extend fun`s across modules fail to link — FIXED

This is the measured blocker for rewrite stage 10 (`docs/design/compiler-rewrite.md`
records it in that stage's Notes). It is **not** an import-system gap and **not**
a missing language feature — `extend fun` parses and lowers correctly under the
current gen2, so no promotion is needed to fix it.

`gen_extend_method` (`codegen/methods_body.sf:600`) registers
`Ctx__m → core_Ctx__m` in `func_prefix_map` only as it lowers that method. The
call site — `resolve_method_symbol` (`methods_body.sf:759`, duplicated inline at
`:940`) — tries `func_prefix_map`, then `current_prefix + name`, then a suffix
scan of `known_functions`, and when all three miss it does `return full`,
handing back the **unprefixed** name:

```
error: use of undefined value '@Ctx__bx'
  %t20 = call i64 @Ctx__bx(i64 %t12, i64 %t19)   ; definition is @core_Ctx__bx
```

Two properties make this specifically a stage-10 blocker rather than a general
annoyance:

- **Prefix-dependent.** The identical forward reference in a *single file*
  compiles and runs correctly, because there `current_prefix` is `""` and the
  guess is accidentally right. It breaks the moment the code lives in a module,
  i.e. exactly when a class is split across files.
- **Order cannot route around it.** One-directional dependencies work if the
  callee is imported first, but the codegen split is *mutually* recursive
  (`gen_arg_value` lives in `expr_body.sf` and is called from `methods_body.sf`;
  `expr_body.sf` calls back into stmt generation). Both orders fail, each naming
  the other direction's symbol.

Verified working, so not part of this bug: `extend fun Ctx.m()` in an importing
file; `this.field` read and write; a core method calling an extension in another
file; extensions calling extensions later in the same module.

**Fix shape, no new machinery needed.** `codegen.sf:737` already walks every
module and computes the correct prefix for each `@extend:` method in order to
fill `extend_map`; it needs to write `func_prefix_map` at the same time. Once
every extension method is pre-registered, the fallback at `:759` is reachable
only on a genuine "not found" — at which point it must become a diagnostic
instead of `return full`.

This is the **fifth** instance of one pattern: a resolution helper that cannot
fail, so it guesses instead of reporting "not found" (#22, #40, #78, #37, and
this). The pattern, not the instance, is the thing worth fixing.

**FIXED (2026-08-02).** `gen_extend_method` registered an extension method's
prefixed symbol in `func_prefix_map` only as it *lowered* that method, so a call
compiled earlier found nothing and `resolve_method_symbol` fell back to an
unprefixed guess — `@Ctx__bx` emitted against a definition named `@core_Ctx__bx`.
Valid IR that dies at link time.

**Why it stayed hidden:** in a single file the module prefix is `""`, so the guess
is *accidentally right* and the defect is invisible. It needed mutual recursion
across a module boundary to surface, since a one-directional call happens to lower
the callee first. Both import orders failed, each naming the other direction's
symbol, so no reordering diagnosed it.

Fixed by `Codegen.prescan_extend_symbols`, which registers every extension
method's emitted symbol before any body is lowered, on all three live paths. The
symbol uses the **extended class's** prefix, not the source file's — an
`extend fun Ctx2.cz()` written in `main.sf` against a `Ctx2` from `core2.sf` must
emit `@core2_Ctx2__cz` — so a shared `extend_class_prefix` derives it once and the
pre-scan and the lowering agree by construction rather than by coincidence.

`resolve_method_symbol`'s `return full` tail is now a diagnostic instead of a
guess, which is the same shape as the rest of this family (#22, #40, #78, #37,
#113): a resolver that could not say "not found" invented an answer.

**Completeness was measured, not argued, before that tail was hardened.** A
temporary `[PRESCAN-MISS]`/`[PRESCAN-DISAGREE]` probe inside `gen_extend_method`
fired whenever a method reached lowering absent from `func_prefix_map`, or present
under a *different* symbol; an instrumented compiler was bootstrapped and run
against a matrix of 8 extension methods spanning class-with-`init` /
without-`init`, extended from its own module / a sibling module / the main
program, mutually recursive pairs within and across modules, and the single-file
path. **Zero MISS, zero DISAGREE.** Only then was the guess replaced. The
hardening was then swept across all 100+ `test/*.sf` and `test/pass/*.sf`: zero
occurrences of the new diagnostic, and the five link failures that appear
(`async`, `docstrings`, `qualified_type_helper`, `test_dns`, `pass/math`)
reproduce identically on the pre-work baseline.

A coverage gap in the first version was found the same way: it walked only
imported-module ranges, leaving main-program and single-file `extend fun`s still
broken.

Also established while mapping the call sites: `generate_with_modules` (`:1289`)
and `generate_with_modules_flat_opts` (`:1969`) have **no callers** —
`main.sf:1394` calls only `generate_with_modules_flat_opts3`. The fourth site,
`Codegen.generate` (`output_body.sf:657`), is live and needed the fix too.

Regression test `test/pass/extend_cross_module.sf` (5 assertions, mutual
cross-module recursion both directions), with its helper in `test/fixtures/`
because `run_tests.sh` globs `test/*.sf` non-recursively and the helper genuinely
cannot link alone. Confirmed against both compilers: the pre-fix one dies with
`use of undefined value '@Ctx__down'`, the fixed one passes 5/5.

### 102. FIXED — an enum's auto-generated `to_string()` segfaulted on a String field, printed every Bool as `true`, and read a one-field enum as its first variant

Found while verifying #77 (`__bool_to_string` double-untag on wasm32). Checking
whether other `to_string` helpers had the same "chosen by static type, handed a
different runtime tag" shape turned up a worse instance on **native**, in
codegen rather than in a runtime base.

```saffron
enum Wide { Num(n: Int), Text(s: String), Flag(b: Bool), Real(f: Float) }

IO.println(Wide.Text("abc").to_string())   // Segmentation fault
IO.println(Wide.Flag(false).to_string())   // "Flag(true)"
IO.println(Wide.Real(2.5).to_string())     // "Real(105553118658576)"
```

```saffron
enum One { Only(s: String) }
IO.println(One.Only("hi").to_string())     // "X()" — no payload, wrong shape
```

`Int` was the only payload type that worked, which is why this survived: every
enum in the compiler's own source and in the test suite either has no payload or
is only ever destructured by `match`, never stringified. The `match` path is a
different function (`extract_arm_bindings`) and is correct.

**Two independent defects in `emit_enum_to_string` (`stmts_body.sf:778`).**

*Encoding disagreement.* Three sites decide how an enum value is laid out, and
they did not agree on the threshold:

| site | condition | encoding chosen |
|---|---|---|
| `gen_enum_construct` (`expr_body.sf:2983`) | `max_fields == 0` | immediate, else heap array |
| `gen_match` (`match_body.sf:204`) | `max_fields == 0` | immediate, else heap array |
| `emit_enum_to_string` | `max_fields <= 1` | immediate, else heap array |
| `emit_enum_constructor` | `max_fields <= 1` | immediate, else heap array |

So for an enum whose widest variant has exactly one field, the constructor built
a heap `[tag, f0]` array and `to_string` decoded it as a `tag << 56` immediate.
`lshr ptr, 56` is 0 for any real heap pointer, which is variant 0's tag — so
**every value of a one-field enum stringified as its first variant**, with the
low 56 bits of the pointer where the payload should be. Silent wrong answer, no
diagnostic, no invalid IR.

*Tag convention per field type.* The payload slot holds a fully NaN-boxed value
(`gen_enum_construct` stores `gen_arg_value`'s result verbatim), while
`__sb_append` takes a **raw** `char*` — it reaches `strlen` through an `@extern`,
so every argument must be untagged. Two of the three arms got that wrong:

- `String`: appended still tagged, so `strlen` dereferenced
  `0x7FF8_0000_0000_0000 | ptr`. **Segfault** — printing any enum carrying a
  String was fatal.
- `Bool`: passed to `__bool_to_string` without untagging. That helper tests
  `icmp ne i64 %b, 0` against a raw 0/1 on three of the four bases, and tagged
  `false` is `0x7FFA000000000000` — nonzero. So **every Bool field printed
  `true`**, `false` included. Exactly #77's mechanism, mirrored: #77 is the
  runtime untagging twice, this is codegen untagging zero times.
- `Float`: no arm at all. Fell through to `emit_untag_int` +
  `__int_to_string`, reading a double's bit pattern as an integer.

**Fix.** All four encoding decisions now read `max_fields == 0`, and each field
arm lands on the convention its callee wants: `emit_untag_ptr` + `ptr_to_val`
for String, `emit_untag_bool` for Bool, and a new arm routing Float through
`__float_to_string` (which untags internally and whose int-tagged path is a
`sitofp`, so a whole-number Float that happens to be int-tagged still formats
correctly — #83).

Covered by `test/pass/enum_to_string.sf`: 11 assertions over both encodings and
every field type, including a four-field mixed variant to catch a per-field
offset error a single-field variant cannot expose. Bootstrap green, STAGE 2
passes, failure set unchanged against the previous run.

**Why the blind spot.** This is [[project_identity_mode_blind_spot]] again, one
layer out: not a runtime helper this time but codegen choosing a *convention* for
one. The compiler bootstraps in identity mode where `emit_untag_ptr` is an
`inttoptr` and tagging is a no-op, so the String arm's missing untag is
literally invisible to `./bootstrap.sh` — and the compiler's own enums are only
ever `match`ed, never printed, so nothing in the self-hosting loop touches this
function at all.

### 101. FIXED — the mark phase rejected every NaN-boxed pointer, so a major collection freed the entire live heap

The collector marked *nothing*. `__gc_is_heap_ptr` rejects anything above
`0x0000FFFFFFFFFFFF` as "a tagged NaN-boxed value or a kernel address" — and
every value the collector reads out of the object graph is exactly that. A heap
pointer in a root slot, a list element, a map key or an instance field reads as
`0x7FF8_0000_0000_0000 | payload`, failed that bounds check, and
`__gc_mark_object` returned without setting a mark bit. The sweep then freed the
whole live heap on the first collection.

Two things hid it, and both were removed in the same afternoon:

- **Identity mode.** The compiler self-hosts against `base.ll`, where tagging is
  the identity, so *its own* roots are bare pointers that pass the bounds check.
  No bootstrap could ever see this (see the identity-mode blind spot).
- **The nursery.** New objects were bump-allocated outside `@__gc_head`, so the
  sweep had nothing to free. Retiring the nursery for #63/#81 removed the
  cover, and the breakage surfaced as strings and map keys losing their first
  byte (`bbbb` → `\xNNbbb`, then length 0), then as a `toml_test` segfault.

```saffron
@extern("void __gc_collect()") fun gc()
import "@toml" as TOML
var basic = TOML.parse("aaaa = 1\nbbbb = 2\ncccc = 3\ndddd = 4")
gc()
var ks = basic.keys()
var i = 0
while (i < ks.length()) { IO.println("key[${i}] len=${ks[i].length()}"); i = i + 1 }
```

Before: `len=4, 0, 0, 0`. After: `4, 4, 4, 4`. The first key survives because it
is still referenced from a live SSA temp at the collection point; everything
reached only through the map is freed under it.

**Fix.** New `@__gc_strip_tag` removes TAG_PTR (`0x7FF8`) and leaves every other
bit pattern alone, so TAG_INT and TAG_SPEC keep failing the pointer test and an
unboxed double is still rejected by the bounds check. `__gc_mark_object` strips
before validating, which normalises both representations it legitimately
receives: NaN-boxed values from the object graph, and raw `__gc_alloc` pointers
for the collector's own internal links (a list's data array, a map's key/value
arrays, a closure's env). `__gc_minor_mark_value` gets the same treatment so
re-enabling the nursery does not resurrect the bug; the forwarding half in
`__gc_minor_visit_slot` is *not* tag-correct and says so in a comment — it needs
a re-tag on the store, and is dead code while the nursery is off.

`test/gc_test.expected` and `test/gc_api_test.expected` both encoded the broken
behaviour and were regenerated: the old baseline literally asserted
`FAIL: No memory freed`, and `gc_test` now reports `PASS: Memory was freed`
(6380 bytes freed across 8 collections, deterministic run to run).

### 100. FIXED — #96's fix was in gen3 but not in gen2, so every bootstrap rebuilt a gen3 that segfaulted on any class

```bash
./bootstrap.sh          # both stages green
echo 'fun g(): Int { return 2 }' > lib/other.sf
printf 'import "@other" as Other\nclass A { }\n' > p.sf
build/saffronc --stdlib lib p.sf out.ll     # Segmentation fault: 11
```

A green bootstrap produced a gen3 that crashed on any program containing a class
*and* an import — which is nearly every real program, including 2 of the suite's
own tests. `--no-check` returned 0, so the fault was in the checker; lldb showed
`EXC_BAD_ACCESS address=0x0` inside `_platform_strncmp`, the signature of reading
an enum field that was never stored.

The mechanism is #96 seen from the other side. `ClassDecl` has six fields and
`docstring` is the sixth; #96 fixed codegen so a match arm binds all of them
instead of the first five. The fix landed in the *source*, so gen3 has it — but
`build/stage2/saffronc` was still the pre-#96 binary, and gen2 is what compiles
the source. So gen2 emitted a `register_decl` whose `ClassDecl` arm never stored
`doc`, `doc.starts_with("@actor")` handed a null pointer to strncmp, and the
resulting gen3 died on the first class it type-checked. Only the checker path
needs an import present, which is why `class A { }` alone survived.

Nothing in the bootstrap could see this. STAGE 1 only proves gen2 accepts the
source. STAGE 2 builds gen4 with gen3 — and gen4 is *correct*, because gen3
compiled it with the fixed codegen, so gen3-crashes-but-gen4-is-fine looked like
gen2 miscompiling whatever change was in flight. Three separate bisections of an
unrelated in-flight change (#98) each ended at "the crash is still here", because
the crash was in HEAD all along: reverting to a pristine HEAD checkout and
bootstrapping reproduced it exactly.

The remedy is the promotion ceremony in CLAUDE.md, which #96 needed and did not
get: `cp build/saffronc build/stage2/saffronc`. Once gen2 knows how to read a
six-field payload, the gen3 it builds no longer crashes and the fixed point holds.

The general rule this makes concrete: **a codegen fix for how the compiler reads
its own AST is not landed until gen2 is promoted.** The committed gen3 was fine;
the committed gen2 was not, and gen2 is the root of trust. A single direct check
catches it — after any bootstrap, compile a program with a class and an import,
not just `test/hello_bootstrap.sf`.

### 99. FIXED — a bare `return` swallowed the following statement as its value

```saffron
fun f(a: String) {
    if (a == "x") return
    var key: String = a + "!"
    IO.println(key)
}
```

```
[codegen] Error: undefined variable 'key'
```

There are no statement terminators in Saffron, so `parse_return` decided a
`return` was bare only when the next token was `}` or eof. Anywhere else it
called `parse_expr` — which consumed the *next statement* as the return value.
The statement then vanished from the enclosing body, so every name it declared
became undefined and the error pointed at the use, never at the return.

Fixed in `parse_return`: a `return` is also bare when the next token is on a
later line. `prev_line_num()` was already there for this kind of check.

Two things made this hard to see. First, the diagnostic names the wrong
construct and the wrong line. In the compiler's own source it came out of the
LLVM verifier as `use of undefined value '%key'` in a function one away from the
cause, which reads exactly like BUGS #97 — a different bug with the same
signature. Second, the failure mode depends on what follows:

- a `var` declaration whose name is used later → `undefined variable`
- an expression statement → **silently miscompiled**: the call becomes the
  return value, so its side effect moves into the guarded path and the guard
  returns the wrong thing
- a bare `return` before `}` → correct, which is the shape everything in the
  tree happened to use

That last point is why this survived: `grep` over the whole tree finds no
pre-existing occurrence of the broken shape. It was reachable but unreached, and
the first two writes of `if (c) return` in `checker.sf` for #98 hit it
immediately.

`checker.sf`'s guards are written `if (c) { return }` rather than
`if (c) return`, because gen2 is still the root of trust for the bootstrap and
gen2 parses the unbraced form the old way. Once gen2 is promoted past this fix
the braces are no longer required.

Test: `test/pass/bare_return_statement_boundary.sf`. Every function in it is
untyped — a bare return yields nil, and the checker correctly rejects nil from a
function declared to return `String`, so the assertions go through observable
effects instead of return values.


### 98. FIXED — a match pattern's arity was never checked, and bindings on an imported enum were all typed Any

Three defects in the same corner of `checker.sf`, each of which silences the
type system rather than mistyping something. Together they mean a `match` on an
imported enum was, in practice, unchecked.

**(a) Pattern arity was never compared against the declaration.**

```saffron
enum E { A(x: Int, y: Int), B }
var e: E = E.A(1, 2)
var r1: Int = match (e) { A(x) => x        B => 0 }   // 1 binding, 2 fields
var r2: Int = match (e) { A(x, y, z) => z  B => 0 }   // 3 bindings, 2 fields
```

Both compiled and ran. Too few bindings left the trailing fields unread; too
many invented a binding whose alloca was never stored, so it read as 0. No
diagnostic either way.

This is how the stdlib AST mirrors drifted. `src/lib/ast.sf`,
`src/lib/formatter.sf` and `src/lib/lang.sf` all matched `ClassDecl` with five
bindings; `tools/gen_docs.sf` used four. The declaration has had six since #95
and five before that, so gen_docs was already reading `parent` as `fields` and
`fields` as `methods` — and had been for as long as the fifth field existed.

**(b) Field types were split on every comma, not top-level commas.**

`enum_fields` stores a variant's payload as one string, `"name:Type,name:Type"`.
`get_enum_binding_type` split it with `.split(",")`, so a field declared
`Map<String, Int>` became two entries and every field after it had its type
read off by one position. Codegen got this right — `match_body.sf` uses
`split_respecting_generics` — so the checker and codegen disagreed about what
type each binding had. The checker now uses `split_type_args`, which it already
had for exactly this.

The same function also split `"name:Type"` with `.split(":")` and took part
`[1]`, which truncates any type containing a colon. It now takes everything
after the first colon.

**(c) A qualified enum name never resolved, so every binding was Any.**

```saffron
import "@ast" as AST
var n: Int = match (stmts[0]) {
    VarDecl(name, t, i, d) => name    // name is String. No error.
    _ => 0
}
```

`enum_fields` is keyed by the *declared* name (`"Stmt"`), but a subject's
inferred type is the qualified spelling the importer sees (`"AST.Stmt"`).
`get_enum_binding_type` looked up `"AST.Stmt.VarDecl"`, missed, and returned
`Any` — for every binding of every arm. `check_exhaustiveness` had open-coded
the prefix-stripping fallback and so worked correctly, which is why
non-exhaustive matches on `AST.Stmt` *were* reported while nothing else about
those matches was. The identical local enum errored as expected, which is what
made this hard to see: the checker looked like it worked.

Fixed by extracting `resolve_enum_key_name`, used by all three call sites, plus
`get_variant_fields` as the single place that parses a payload string. New
`check_pattern_arity` runs on both `match` arms and `let`-destructuring, which
go through separate paths in the checker.

Tests: `test/fail/match_pattern_arity.sf`, `match_pattern_arity_excess.sf`,
`enum_binding_type_generic_field.sf`, `enum_binding_type_qualified.sf`. All four
compile cleanly under the pre-fix gen2 and are rejected by the fixed compiler,
which is the property that makes them worth having — a test in `test/fail/` only
proves *something* was rejected.

The (c) tests need care to avoid passing vacuously: with one arm String and one
arm Int the result unifies to `String|Int` and an assignment to `Int` is accepted
for a legitimate unrelated reason. Both arms have to return the same wrong type
for the binding's own type to be what is under test.

Consequence of turning (a) on: the four stdlib/tool mirrors above had to be
corrected to six-field `ClassDecl` patterns in the same change, since they now
fail to compile. That is the point — they were wrong before and nothing said so.


### 97. FIXED — a module global assigned inside a closure was treated as a capture, emitting IR that did not verify

```saffron
var top: Int = 0
var top_bump = fun (): Int => {
    top = top + 1
    return top
}
IO.println(top_bump())
```

```
opt: output.ll:2649:25: error: use of undefined value '%top'
    %t95 = load i64, i64* %top
```

`find_free_vars_expr`'s `Assign` arm counted a module-level `var` as a free
variable of the enclosing lambda, so `gen_lambda` built an env for it. The env
store loop then emitted `load i64, i64* %top` in the *enclosing* function — but
`output_body.sf` deliberately skips the alloca for a name that is a module
global, because a global is addressed as `@__g_top` from wherever it is
mentioned. Nothing in the frame to load, hence an undefined value and a program
the verifier rejects.

The read side has always excluded globals: the `Variable` arm's second condition
is `this.typed_vars.has(name) and !this.module_globals.has(name) and !...`. The
`Assign` arm instead *opted them in*, with an explicit
`or this.module_globals.has(this.current_prefix + name)`. So `top` alone was not
a capture and `top = top + 1` was, which is the asymmetry that made this a bug
rather than either half being wrong on its own.

The store was always correct. `gen_assign` resolves the target to `@__g_` on its
own (stmts_body.sf, the `current_fn_locals` check), so the `%top` slot the
capture machinery set up was dead code inside the lambda; the failure landed in
the enclosing function, which is why the error pointed away from the closure.

Fixed in `codegen/closures_body.sf` by excluding module globals in the `Assign`
arm exactly as the `Variable` arm does — checking both the bare and the prefixed
key, since the bare form is what `module_globals` holds for a main-program global.

Latent, not a regression. The same source at HEAD produces the same broken IR
when compiled by a compiler built from it: the checked-in gen3 binary predated
the #51-defect-B boxing work that made `Assign` captures reachable at all for a
global, so the shape was unreachable in the binary while sitting in the source.
Test: `test/pass/closure_capture_writeback.sf`, whose top-level section exists to
catch exactly this.

### 96. FIXED — a match arm bound at most five enum fields, silently truncating wider payloads

```saffron
enum S {
    Six(a: String, b: List<String>, c: String, d: String, e: String, f: String),
    Other
}
fun probe() {
    var s: S = S.Six("A1", ["T"], "C3", "D4", "E5", "F6")
    IO.println(match (s) { Six(a,b,c,d,e,f) => f
        _ => "?" })     // printed 0, not "F6"
}
probe()
```

`extract_arm_bindings` (`codegen/match_body.sf`) was five unrolled lines —
`if (bind_count >= 1) ... if (bind_count >= 5)` — commented "Extract each field
individually to avoid VM loop variable bug". The VM in question is the C bytecode
VM in `legacy/`, dead and unsupported. The workaround outlived its reason and had
quietly become an arity limit.

Nothing rejected a sixth field. `gen_enum_construct` stores every argument in a
plain loop, so the payload was *built* correctly; only the read-back stopped at
five. The sixth binding kept whatever its freshly-allocated slot held, which reads
as 0. A 0 standing in for a String is a null pointer, and the next `starts_with`
lowers to `strncmp(NULL, ...)` — so the failure surfaced as a SIGSEGV in an
unrelated function rather than as a wrong value.

This is what made #95 look like a gen2 miscompilation. `ClassDecl` gained
`type_params` as its second field, pushing `docstring` to sixth; the codegen
pre-scan's `cd3.starts_with("@actor")` then dereferenced NULL, and *any* program
containing a class crashed the new compiler. gen3 segfaulted while gen4 did not,
purely because the two were built by compilers on opposite sides of the cap.

Fixed by replacing the unrolled chain with a `while` over `bind_count`.
`extract_one_field` already took the index as a parameter, so the loop needed no
other change.

### 95. FIXED — the parser discarded a class's generic parameters, so no method returning one could be typed

```saffron
class Box<T> {
    var v: T
    fun init(v: T) { this.v = v }
    fun unwrap(): T { return this.v }
    fun getItem(key: Int): T { return this.v }
}
var b: Box<String> = Box("hello")
var u = b.unwrap()     // [checker] Warning: u: cannot infer type
IO.println(b[0])       // segfault
```

`parser.sf` consumed the `<T>` of a class header under a comment that said
`// Skip generic type parameters` and threw the names away, and `ClassDecl` had no
field to put them in. After parsing, `T` did not exist anywhere. A method
declared `fun unwrap(): T` therefore registered "T" as its return type — a name
with no representation — and every consumer had to cope with that or guess.

The source states this information. `Box<String>` says what `T` is. Discarding it
at the parser and then trying to recover it downstream is what made the types
incomplete, and the two workarounds that grew in its place were both wrong:

- **codegen `gen_method_call`** treated any return type of one or two uppercase
  characters (excluding `IO`/`OS`) as a type parameter and substituted the
  *entire* contents of the receiver's angle brackets. It missed a spelled-out
  parameter like `Element`, and on a `Pair<Int,String>` it set `last_type` to the
  string `"Int,String"`, which names nothing.
- **codegen `gen_index_get`** (the #94 fix) looked up `class_fields.has("Box<String>")`
  against a table keyed by the bare name, so the test was always false, the
  `getItem` arm was skipped for **every generic class**, and the receiver fell
  into the list path that #94 was filed about. #94 was fixed for `Dict` and still
  broken for `Box<T>`.

Fixed at the source of the loss:

- `ast.sf` — `ClassDecl` gains `type_params: List<String>` as its second field.
- `parser.sf` — the skip-loop collects the parameter names at depth 1 instead of
  discarding them. A nested constraint (`T: Comparable<U>`) is still skipped;
  only the parameter names participate in substitution. This also fixed a
  pre-existing arity bug at what is now `parser.sf:2761`, where a `ClassDecl`
  pattern bound 4 of 5 fields — latent rather than loud because of #76.
- `checker.sf` and codegen both register `class_type_params`, keyed by the bare
  class name like every other class table, and substitute positionally:
  `substitute_class_type_params("T", "Box<String>")` → `"String"`,
  `("List<V>", "Pair<Int,Str>")` → `"List<Str>"`. A parameter the annotation
  leaves unbound comes back **unchanged**, not as `Any`, so a caller can tell
  "resolved" from "the source did not say".
- Substitution is applied at method calls, field reads (all five field-read paths
  in `expr_body.sf`/`methods_body.sf`), and subscripts.
- Every table lookup that took a receiver type now goes through
  `generic_base_name`, so `Box<String>` resolves like `Box`.

Regression test: `test/pass/generic_class_type_params.sf` (16 assertions —
method/field/subscript, one and two parameters, reversed positions to prove the
binding is positional, a multi-character parameter name, a parameter nested in
`List<T>`, and an untouched non-generic class). Every assertion calls a
type-specific method on the result, because a wrong return type is not
necessarily a crash — it can also be a silently dropped call.

### 94. FIXED — `obj[key]` on a class declaring `getItem` was compiled as a list read

```saffron
class Dict {
    var items: List<String>
    fun init() { this.items = ["first", "second", "third"] }
    fun getItem(key: Int): String { return this.items[key] }
}
var d = Dict()
IO.println(d[0])   // segfault
```

`Indexable<K, V>` in `src/lib/prelude.sf` has declared this protocol since the
prelude was written, and `test/pass/getitem_overload.sf` has tested it for just as
long, but **nothing ever implemented it**: `getItem` appeared nowhere in the
compiler. `gen_index_get` handled Map, then String, then fell through to a
catch-all list path that called `__list_length` on the instance pointer, reading a
class struct as a list header — SIGSEGV.

Structurally the same defect as #93: a receiver type no arm recognises silently
takes the List arm. The checker was the milder half of it — `IndexGet` ran
`extract_list_element_type` on a class and got "Any", harmless in itself but it
told the following dispatch nothing.

Fixed by adding a `getItem` arm to `gen_index_get`
(`src/compiler/codegen/methods_body.sf`), placed *above* the receiver evaluation
so it is decided before any IR is emitted (the preamble hazard of #70), and by
reading `ClassName__getItem`'s registered return type in the checker's `IndexGet`
case. Class resolution mirrors `gen_binary`'s operator-overload path, including
its fallback for a non-variable receiver, so `Dict()[0]` works too.

`setItem` is deliberately **not** implemented: unlike `getItem` it is specified
nowhere — not in the prelude, not in any test — so there is no contract to
implement against. `obj[k] = v` on a class remains whatever `IndexSet` already
did.

Regression test: `test/pass/getitem_overload_typed.sf` (13 assertions — values,
computed keys, a constructor-call receiver, a non-String element type, and
untouched List/Map/String indexing). `test/pass/getitem_overload.expected` pins
the original repro's output.

**This fix was incomplete as first committed** (`eac1ccc`): the class lookup used
the receiver's full type spelling against tables keyed by the bare name, so a
generic class (`Box<T>`) skipped the new arm entirely and still segfaulted. See
#95, which fixes the root cause — the parser discarding `<T>` — and the lookup.

### 93. FIXED — a method call on a nil-guarded nullable receiver dispatched as a List, or was dropped entirely

```saffron
fun find(name: String): String|Nil {
    if (name == "found") { return "hello world" }
    return nil
}
var r: String|Nil = find("found")
if (r != nil) {
    IO.println(r.length().to_string())   // segfault
    IO.println(r.to_upper())             // silently emitted nothing
}
```

The checker gets this right: it narrows `String|Nil` to `String` inside the
guard, and it rejects the unguarded call outright ("cannot call .length() on
nullable 'r' (type String|Nil); add a nil check first"). But that narrowing lives
in the checker's own `env.narrowed` map and is never written back to codegen's
`typed_vars`, so `gen_method_call` still saw the literal spelling `String|Nil`.

Every builtin dispatch arm matches the receiver type by exact name — `"String"`,
`starts_with("List")`, `starts_with("Map")` — so the union spelling matched
nothing, and the two ways of missing produced two different failures:

* `length` ended in a catch-all `else` that assumed List. `__list_length` does
  `inttoptr` on the value exactly as passed; a List is stored untagged but a
  String is NaN-tagged, so it dereferenced `0x7FF8...` and took SIGSEGV. This
  crashed `test/nullable_narrowing.sf`.
* `to_upper`, `trim`, `split`, `index_of` and the rest guard on
  `type == "String"` with no fallback, so they emitted **nothing at all** — the
  receiver was loaded and discarded and the call site produced a null operand.
  That one is worse than the crash: it is invisible to any test that only checks
  an exit code, which is most of them (cf. #90).

Fixed by `strip_nil_type_str` + `dispatch_recv_type` in
`src/compiler/codegen/types_body.sf`, threaded through the ten dispatch-preamble
sites in `methods_body.sf`. Dropping `Nil` in codegen cannot mask a real nil
access, because the checker has already proven non-nil at every call site that
reaches here. A genuine multi-member union (`Int|String`) is left alone: it has
no single correct arm, and guessing one is the bug being fixed. `length` on an
unresolved receiver now routes to `__any_length`, which reads the GC type tag and
picks list/map/strlen at runtime, instead of assuming List.

Regression test: `test/pass/nullable_recv_dispatch.sf` — it compares values
rather than exit codes, for the reason above.

### 91. FIXED — an `@extern` reached through a module alias was called by its Saffron name, which has no definition

```saffron
import "@scheduler" as Scheduler
IO.println(Scheduler.get_yield_reason().to_string())
// use of undefined value '@stdlib_scheduler_get_yield_reason'
```

`get_yield_reason` is `@extern("i64 __sched_get_yield_reason()")`. Reached through
a module alias, codegen emitted a call to the *Saffron-level* symbol rather than
the C symbol the `@extern` names, and emitted no `declare` for it either, so the
IR failed verification.

The mechanism is the same one behind #38: `gen_method_call`'s "Universal module
dispatch" arm (`methods_body.sf:1259`) intercepts every alias with a non-empty
prefix, and it consulted neither `extern_sigs` nor `intrinsic_funcs`. The arm
that does both sits at `:1630` and so never saw a prefixed alias — it only ever
ran for empty-prefix builtins. Fixed by hoisting the intrinsic and extern checks
to the top of the universal arm, before `called_functions.push`, so the extern's
C symbol is what gets declared.

This is why #38 had to be diagnosed by instrumenting `scheduler.sf` in place:
probing scheduler state from a test program was impossible. Regression test at
`test/pass/module_extern_dispatch.sf`, which also covers a plain function and an
extern with an argument through the same arm.

### 90. FIXED — `run_tests.sh` only checked the exit code, so a test could pass while producing almost no output

A test that printed 2 of its 12 expected lines and then exited 0 was reported
PASS. `test/test_async.sf` was green for the entire life of #38 while
`Async.sleep` silently did not suspend and the fan-out section printed garbage
(`1^2 = 4.94066e-324`) — the suite never looked at the output at all.

Fixed with an optional `<name>.expected` file per test, diffed against
stdout+stderr through the same `filter_noise` normalization the failure scanners
use. Opt-in per test rather than a blanket requirement: assertion-based tests
need no such file, since `@test` already sets a non-zero exit, and back-filling
165 files would mostly freeze output nobody has verified.

31 `.expected` files were added, covering the assertion-free smoke tests whose
value *is* their output. Deliberately left without one:

- the 14 tests that currently emit errors — freezing a broken output as
  "expected" is worse than not checking it;
- `gc_roots_test` and `test_httpx`, whose output genuinely varies run to run;
- `functions`, `nullable_narrowing` and `test_reflect`, which segfault (the
  suite's `segfault` check already catches those, and a `.expected` would only
  record the crash as correct);
- the `mini_*` tests, whose result is their exit code.

Each candidate was run twice and diffed before its file was kept, so a
nondeterministic test cannot enter the set. Verified the mechanism actually
fires by pointing `test_async.expected` at wrong content and confirming an
`output-mismatch` failure.

### 88. FIXED — `OS` was a reserved name, not an import alias: `import "@os" as Platform` failed and os.sf's own functions were unreachable

**Reproduction:**

```saffron
import "@os" as Platform
IO.println(Platform.path_sep())
```

```
[codegen] Error: undefined variable 'Platform'
```

The same file with `as OS` compiled and ran. So the alias in an `import ... as`
was not actually an alias — one specific spelling was required, and only because
codegen happened to have that spelling wired to a symbol prefix.

Two mechanisms, both needed for the failure:

1. `is_builtin_module` (`main.sf:448`) returned true for `"os"` and `"@os"`, and
   is consulted at 13 sites that gate whether a module is loaded at all. So
   `src/lib/os.sf` was **never loaded** — the alias never entered `alias_map`,
   which is why any alias at all was an undefined variable. The name suggests a
   C module behind it, but `resolve_import_path` maps both `"os"` and `"@os"` to
   `src/lib/os.sf`; there is no C `os` module and never was. `io` was never
   listed here, which is why `import "@io" as Files` always worked.
2. `module_prefixes.set("OS", "__os_")` (three copies in `codegen.sf`, ~545,
   ~1036, ~1654) hard-wired the literal identifier `OS` to the `__os_` prefix.
   `module_prefixes` outranks the import alias in the lookup order
   (`methods_body.sf:2727-2736`: `named_imports` → `module_prefixes` →
   `func_prefix_map` → guessed prefix), so even with os.sf loaded, `OS.foo()`
   lowered to `@__os_foo` rather than to the module's function.

Consequence (2) is the worse one, and it is a trap for anyone extending the
stdlib: a function *written in Saffron* in os.sf can never be called. It lowers
to `@__os_<name>`, a runtime symbol that does not exist, and fails at link time.
`OS.join_path` hit exactly this — the obvious "fix" is to reimplement it in the
runtime, which is treating the symptom and pushes pure-Saffron logic into
`runtime.sf` for no reason.

**Fix:** drop both. `is_builtin_module` now returns false unconditionally (it has
no other entries), and the three hard-wired `OS` prefixes are removed. `IO`, `GC`
and `Reflect` keep theirs deliberately: `IO` is implicit in every file with no
import statement to derive an alias from, so it has no `alias_map` entry to fall
back on.

Introduced in `70c7ee9` (2026-05-26).

**`IO` had the same reserved-name defect, in a narrower form** (found by asking
whether `IO` was really safe to keep hard-wired; fixed as part of the same
change). `IO` genuinely needs a hard-wired prefix — a file with no imports still
calls `IO.println`, so there is no import statement to derive an alias from — but
the three `module_prefixes.set("IO", "__io_")` calls ran *after*
`gen.module_prefixes = alias_map`, so they overwrote an explicit alias too.
Binding the name `IO` to the module with `import "@io" as IO` therefore sent every
call to a runtime symbol instead:

```saffron
import "@io" as IO
var f: IO.File = IO.open("/tmp/x", "r")   // ld: symbol not found: ___io_open
```

Five functions are written in Saffron in `src/lib/io.sf` with no runtime symbol
behind them — `open`, `bytes_alloc`, `bytes_from_string`, `read_file_bytes`,
`write_file_bytes` — so each failed at link time with an undefined `___io_<name>`
and no diagnostic. Naming the alias anything else (`as Files`) worked, which is
what made this easy to miss and why it survived #88's first pass.

Fixed by guarding the hard-wiring with `if (!alias_map.has("IO"))`, so an explicit
import reaches the module exactly as any other alias does while the implicit
spelling keeps the runtime prefix. Regression test at
`test/pass/io_explicit_alias.sf`.

Two other repairs were considered and rejected. Routing an unknown `__io_<m>` to
`stdlib_io_<m>` inside the dispatch path would also redirect `IO.println` — the
most-called function in the language — onto the module wrapper, changing its
arity and tagging contract for every program. Deriving the fallback from
`func_prefix_map` is worse: that map is global and keyed on the bare method name,
so two modules that both define `open` collide there and `IO.open` could route
into an unrelated module.

**A third defect, exposed by the above** — once `import "@io" as IO` reached the
module, `IO.println` reached io.sf's own `println`, which printed
`2.12529e-314`. The wrapper forwarded through an `@extern("void
__io_println_any(i64)")`, and every `i64` extern parameter is untagged on the way
out (`gen_extern_call`, deliberately — BUGS #24). `__io_println_any` dispatches on
the NaN-box tag, so the extern stripped the very tag the callee reads and the tag
bits landed in the payload. `print` had it too. Both now call `__io_println` /
`__io_print` by bare name, matching what `read_file` already did and what os.sf
was converted to above; both names had to be added to `known_functions` as well,
or the bare-call path prefixes them to `stdlib_io___io_println` (the #85 shape).
`println_str` keeps its extern — `puts` is typed `void*`, so it untags a pointer,
which is correct.

**Resolution (2026-07-31).** os.sf is an ordinary module. Regression test at
`test/pass/os_ordinary_import.sf` covers a non-`OS` alias resolving, `"os"` and
`"@os"` reaching the same module, and `join_path` — pure Saffron with no runtime
symbol behind it, so it only links when the module is genuinely loaded. The
compiler's own 13 `OS.*` call sites (`main.sf:6` does `import "os" as OS`) now go
through the module instead of the prefix, so the bootstrap exercises the new path
end to end.

**Second defect, exposed by the first fix.** Routing `OS.*` through os.sf made
`OS.path_sep()` return a bare address that printed as `105553182851136` and
compared unequal to `"/"`. Not a regression in the new path — a latent defect in
os.sf that the hard-wired prefix had been hiding, since it bypassed the module
entirely. Every string-returning `__os_*` in `runtime.sf` already pointer-tags
its result with `__rt_tag_ptr`, but os.sf reached them through
`@extern("i64 __os_cwd()")` declarations, and an `i64` extern return is
NaN-box *int*-tagged on the way back — so an already-tagged pointer got tagged a
second time. `src/lib/io.sf` never had this because it calls its `__io_*`
counterparts by bare name, which passes the value through unchanged. os.sf now
does the same, and the `@extern` block is gone (only `exit` remains, a genuine
libc symbol). `__os_system` had to join the known-function tables for its bare
call to survive prefix mangling; the stale caveat on its docstring — that its
result "reaches Saffron untagged" and "compares unequal to any literal" — is
removed, because that was this bug, not a property of `system()`.

**Third defect, also exposed, and wider than `os`: every int-returning stdlib
wrapper reached through a module alias returned a subnormal double.** This one is
BUGS #23 hitting the `@io` and `@os` module paths, and it was fully live before
any of this work — `import "@io" as Files` then `Files.file_size(p)` printed
`5.43472e-323` for an 11-byte file, and `Files.file_exists(p)` was **false for a
file that exists**. The second is the dangerous one: a raw `1` does not compare
equal to a NaN-boxed `1`, so the wrong answer is silent. It went unnoticed
because `IO.file_size` — the implicit-namespace spelling — has its own tagging
path in `methods_body.sf` and is correct, so only the module-alias spelling was
affected, and nothing in the suite used it. De-specializing `os` moved
`OS.file_exists` onto that broken path, which is how it surfaced.

Patched at the boundary: a `_retag(n) => n + 0` helper in each of `io.sf` and
`os.sf`, applied to `file_size`, `read_binary`, `file_exists`, `is_dir`,
`rename`, `delete_file`, `set_env` and `system`. Arithmetic forces the value
back through the int path and re-tags it — the same mechanism `Bytes.get`
documents for `& 255`. Verified against a 3 MB file, so it is not an artifact of
small values rounding back. It is a helper rather than an inline `+ 0` at each
site so that the next wrapper added does not silently omit it. This is a patch,
not a fix: the real repair is #23 (tag on the way out of the runtime), and the
helper carries a comment saying to delete it then. Regression test at
`test/pass/io_module_wrappers.sf`.

The wrapper patch only covers the module-alias spelling. The implicit
`IO.`/`OS.` namespace path has its own tagging table
(`methods_body.sf:~1400-1430`), which had arms for string- and bool-returning
runtime functions but none for int-returning ones — so `IO.file_size(p)` printed
`5.43472e-323` and `== 11` was false there too. Added an int arm covering
`__io_file_size`, `__io_read_binary` and `__os_system`.

**Deliberately not added: a list arm.** Tagging the returned list pointer for
`__io_walk_dir` / `__os_args` segfaults, which is #23's GC caveat firing exactly
as documented — `__gc_is_heap_ptr` (`gc.ll:615`) rejects a NaN-tagged value as a
heap pointer, so a tagged `__list_new()` result stops being traced and is swept
while live. The container must stay untagged. `IO.list_dir(d)[0]` returning a
subnormal was the same bug one level down: `__io_walk_dir` pushed *untagged*
strings, where `__os_args` and `__str_split` both tag each element. Fixed there,
in the runtime, per #23's "tag at the boundary in runtime.sf, never in codegen".

**Two more defects found while re-running the stdlib suites** (both pre-existing,
both in the blast radius, so both fixed rather than filed):

- `__os_platform` returned `"macos"`. Everything that consumes it expects the
  uname spelling `"darwin"`: `docs/src/stdlib/os.md` documents it that way, both
  `stdlib_os` suites assert it, and `src/lib/tar.sf`'s `_is_macos()` compares
  against `"darwin"` — so it was dead code that always returned false, and
  `tar.sf` silently took the GNU branch on macOS, emitting `tar --transform` and
  `stat -c` where BSD tar needs `-s` and `stat -f`. Now `"darwin"`. Still
  hardcoded, so still wrong on Linux; real detection needs `uname(2)`, whose
  `struct utsname` has no portable layout to hand-offset into from Saffron (the
  same reason `__io_is_dir` probes with `opendir` rather than `stat`). That wants
  a C helper and is its own change.
- `test/stdlib_os.sf` asserted `OS.env(missing) == nil`. The documented contract
  is an empty string, the return type is `String` (not `String|Nil`, so `nil` was
  never a possible value), and `__os_env` returns a tagged `""` on a NULL
  `getenv`. The test was wrong, not the implementation; corrected to expect `""`.

### 87. FIXED — a grandchild that declares no `init` called a zero-arg constructor and dropped its arguments

**Reproduction:**

```saffron
class Animal { var name: String
    fun init(name: String) { this.name = name }
    fun describe(): String { return this.name } }
class Dog extends Animal { fun speak(): String { return "Woof" } }
class Puppy extends Dog { fun speak(): String { return "Yip" } }

IO.println(Puppy("Max").describe())   // segfaults — this.name is 0
```

`Dog` inherits `Animal.init` via the forwarder at `stmts_body.sf:388-418`, which
emits `@Dog__init` as a thin call to `@Animal__init`. But that forwarder never
calls `called_function_arity.set` for `Dog__init`. So when `Puppy` is lowered
and the `init`-forwarding guard at `stmts_body.sf:384` asks
`called_function_arity.has(<prefix>Dog__init)`, the answer is false and `Puppy`
gets no `init` forwarder at all. `Puppy("Max")` then compiles to a bare
`@Puppy()` with the argument silently dropped (the same shape as the one-level
case #50's forwarder comment describes), the fields stay 0, and the first field
read through an inherited method segfaults.

One level of inheritance works because `Animal__init` is a real definition that
registers its arity; the break is specifically at the *second* level, where the
parent's `init` is itself a forwarder. Found while verifying #50's `List<Animal>`
case with a `Puppy extends Dog extends Animal` chain.

**Workaround:** give every level an explicit `init`. **Fix:** have the `init`
forwarder register `called_function_arity` (and the prefixed key) for the
`Child__init` it emits, so the next level down sees it. Independent of virtual
dispatch — the dispatch itself is correct once the object is constructed.

**Resolution (2026-07-31).** The forwarder (`stmts_body.sf`) already computed
the parent's arity to build its parameter list; it now also writes that arity to
`called_function_arity` for both the bare and prefixed `Child__init` keys, the
way `gen_class_method` does for real methods. A two-level chain then sees the
immediate parent's `init` arity and forwards correctly. Verified: `Puppy("Max")`
with `init` only on `Animal` constructs and prints `Max: Yip`; the `List<Animal>`
polymorphism test runs end to end. Zero regressions.

### 89. FIXED — `src/lib/string.sf` declared `class String`, which the builtin `String` shadowed, so every method taking one read a field off a value with no fields

Inside `class String`, parameters were spelled `sub: String`, `prefix: String`,
`delim: String` — and the **builtin** `String` won name resolution, not the
class declared twenty lines above. Each of those parameters was a builtin
string, the body then did `sub._ptr`, and a builtin string has no `_ptr` field.
Eighteen such reads across `contains`, `starts_with`, `ends_with`, `index_of`,
`split`, `replace`, `add` and `eq`. Only `length()`, `char_at()` and the
no-argument transforms worked, because those touch nothing but `this`.

It stayed hidden because nothing in the tree imports `@string`. The module is a
`docs/design/runtime-v2.md` Phase D prototype, and a prototype with no callers
gets no coverage; a shadowed type name produces no diagnostic at all.

Fixed by **renaming the class to `CStr`**, so its parameter types name it
unambiguously. The alternative — a scoping rule where a class declaration
shadows a builtin type name of the same name inside its own module — was
rejected: it is a language-wide change to name resolution touching the checker,
codegen and the import resolver, and its effect would be that `class String { }`
anywhere silently retargets every `String` annotation in that module. That is a
large new footgun bought to avoid a rename that breaks no caller, and the class
is not a replacement for the builtin anyway. The same shadowing still exists in
`src/lib/int.sf`, `float.sf`, `bool.sf`, `list.sf` and `map.sf`, all equally
unimported and untested.

Two more defects surfaced once the methods could run. The string-returning
runtime externs were declared `: Int`; each already pointer-tags its result with
`__rt_tag_ptr`, so `: Int` made codegen int-tag a second time and `trim()`,
`to_upper()`, `to_lower()` and `repeat()` returned bare integers that printed as
addresses (`105553145500536` instead of `"hi"`) — the hazard `src/lib/os.sf`
documents at the top of the file. And `to_string()` was `return this`, handing
back the CStr where its own signature promised a String.

Three methods are now deliberately absent rather than present and wrong:
`to_number()` called `_int_to_string`, the integer→string direction and the exact
inverse of its docstring; `add(other)` was `this._ptr + other._ptr`, the sum of
two addresses; `eq(other)` was pointer identity, so two CStrs over equal bytes at
different addresses compared unequal. A caller now gets "no method 'eq'" at
compile time instead of a confident wrong answer at runtime. They return when
Phase D gives them implementations.

Covered by `test/pass/checker_cstr_module.sf` — 16 assertions over exactly the
methods that used to segfault, checking their *values*, since a
pointer-comparison bug would still "run". The old repro spelling
`Str.String("hello world")` is now `Str.CStr("hello world")`.

### 86. FIXED — a block-syntax parameter got no type, so every field read on it silently answered 0

`apply_block(box) { x => "v=" + x.v.to_string() + " n=" + x.name }` compiled
cleanly and printed `v=0 n=`. The same lambda written `fun (x: Box): String =>
...` printed `v=42 n=hello`. No error — only a `dispatching '...' on untyped
value` warning, which scrolls past in a build log.

`parser.sf`'s `build_lambda` constructs every block parameter as
`AST.Type.AnyType`, because a block has nowhere to write an annotation:
`{ x => ... }` is only a name and a body. Nothing downstream ever filled it in,
so codegen resolved `x.v` against no class and answered the guessed type's zero
value — 0 for Int, `""` for String.

A block's parameter types can come from exactly one place: the parameter it is
being passed to. The checker now records, per callee parameter, the declared
parameter types of any function-typed parameter in a `fun_param_sigs` side table
keyed `"funcname:index"` / `"Class__method:index"` (the `receiver_params`
convention), and rewrites the `Lambda` node's `Param`s with those types before
codegen sees them. Three states are distinguished on purpose, because "we could
not find a type" and "the source does not contain a type" are different bugs:
an absent key means the parameter is not a function type; `"?"` means a bare
`Fun`, which declares nothing about the callback and is therefore an **error**
rather than a guess; otherwise the declared types are written in. Only `AnyType`
parameters are touched, so an explicitly annotated lambda keeps its own types
and a genuine mismatch is still reported as a mismatch.

Two details that cost a bootstrap to find. The signature must be rendered with
the checker's own `type_to_str`, not `AST.type_to_string`, which collapses
`GenericType(base, args)` to `base` and every `FuncType` to `"Fun"` — so
`Fun<Box, String>` arrives already stripped and every callback looks bare. And
method signatures are registered *only* under the receiver-qualified key, unlike
`receiver_params` which also registers the bare method name: a bare key is
ambiguous across classes (`Map.get(key)` vs `App.get(path, handler)`), and an
ambiguous hit here does not merely fail to help, it raises an error against
another class's signature.

`Fun<A, B, R>` was, at the time of this fix, the only spelling that could type a
block. `(A, B) => R` could not, and that was a separate defect — now also fixed,
see the addendum below: `parse_single_type` encodes it
into the string `"Fun(A,B):R"` and then `parse_type_ast` discards it with
`if (s.starts_with("Fun(")) { return AST.Type.FuncType([], AST.Type.AnyType) }`
(src/compiler/parser.sf ~line 1014). Fixing that one line would make `(A) => R`
work with no further checker change.

Covered by `test/pass/checker_block_param_typed.sf` (7 assertions, which check
that the block form and the annotated form agree on identical input — the
pre-fix compiler accepted the file) and `test/fail/checker_block_param_untypeable.sf`
(a bare `Fun` callee must be rejected).

**The arrow half is now FIXED too (2026-08-02) — and the prediction above was
wrong.** This entry claimed "fixing that one line would make `(A) => R` work with
no further checker change." It took **two** further changes, and one of them was a
silent-wrong-answer bug of its own:

1. `parser.sf` now parses a function type's parameters and return type instead of
   discarding them, so `(A, B) => R` produces a populated `FuncType` node.
2. That populated node immediately broke `codegen.sf`'s paren-blind parameter
   split — **#114**, which made `reduce` answer 12 instead of 10 in shipped
   stdlib. The parser fix alone is *harmful*; the two must land together.
3. The arrow spelling still could not type a block, because
   `register_fun_param_sigs` rendered the parameter through `type_to_str` first,
   and `type_to_str` collapses **every** `FuncType` to the bare `"Fun"`. The
   populated node was flattened before `fun_param_types_of` ever saw it, landing
   the arrow spelling in the `"?"` state — which this entry defines as an *error*
   ("declares nothing"), not a guess.

Point 3 was fixed by reading the **node**, in a new `fun_param_sig_of`, rather
than by making `type_to_str` richer. That restraint was load-bearing:
`type_to_str` has three consumers that depend on its collapsed `"Fun"` and would
have regressed silently. `infer_expr` renders a Lambda's inferred
`FuncType([], Any)` through it and `is_type_param` tests `typ == "Fun"` by exact
equality — so a rich `"Fun():Any"` would make `is_type_param` answer **true**,
reclassifying every lambda as a generic type parameter. `fun_param_types_of` tests
`type_str == "Fun"` the same way, and `is_subtype_node` compares two *rendered
strings*, so a rich rendering would stop equating two function types and invent
new errors.

That is exactly the reasoning this entry used when it chose `type_to_str` over
`AST.type_to_string`, applied one level further in — and the reason a previous
crude global probe of `type_to_str` produced a segfault.

`test/pass/checker_block_param_arrow_type.sf` was written to **fail by design**
(6 errors) until all three landed; it now passes 10/10, and
`test/pass/checker_block_param_typed.sf` still passes 7/7, so the `Fun<A, B, R>`
spelling did not regress. Both spellings now work.

### 85. FIXED — `__io_file_size` and `__io_read_binary` were missing from the known-function tables, so calling them from a stdlib module emitted a bad symbol

```
[codegen] Warning: calling undefined function '__io_file_size'
[codegen] Warning: calling undefined function '__io_read_binary'
```

Both runtime functions exist (`src/runtime/runtime.sf:1103-1134`) and both work.
They are absent from the table at `src/compiler/codegen/utils_body.sf:5-6`, so a
call from inside a module gets the module prefix applied — `@io___io_file_size` —
and fails to link. The warning is printed for *any* program that imports `@io`,
including ones that never call either function, which is why it reads as noise
rather than as a real defect.

This is the working binary read path: `IO.file_size` + `IO.read_binary`
(`src/lib/io.sf:255-263`) return the correct byte count where `IO.read_file`
truncates at the first NUL (#66). So the two functions that route around the
binary-data problem are the two that cannot be called normally.

**Fix:** add both names to the known-function list. One line, no risk.

**Resolution (2026-07-31).** Slightly more than one line, because
`known_functions` membership *also* suppresses the auto-generated `declare`
(`output_body.sf:1114`). So both names were added to every `known_functions`
block (`codegen.sf` ×3, `output_body.sf`) to stop the module-prefix mangling,
*and* to the `runtime_declares()` name/sig tables (`utils_body.sf:5-6`,
`i64 @__io_file_size(i64)` / `i64 @__io_read_binary(i64, i64, i64)`) to restore
the `declare`. Verified: `IO.file_size` returns the correct byte count
(`size=11` for `"hello world"`); zero test regressions.

The diagnosis also surfaced, out of scope here: `IO.is_dir` / `IO.rename` /
`IO.delete_file` (`test/stdlib_io.sf`) have no runtime *or* stdlib
implementation at all and reach the linker with no warning, because the
namespace-dispatch path (`methods_body.sf:990-1008`) has no `known_functions`
guard. That missing guard is why this whole class went undetected.

### 84. FIXED — a `void*`-returning `@extern` had its NULL result pointer-tagged, so a `== 0` check was unconditionally false — `IO.open` did not throw on a missing file

The single highest-impact instance is in the stdlib and is user-visible today:

```saffron
import "@io" as FileIO
var f = FileIO.open("/tmp/definitely_missing_file", "r")   // does NOT throw
```

`io.sf:221-227` looks correct — it declares `@extern("void* fopen(void*, void*)")`
(`io.sf:16`), assigns to `var fp: Int`, and guards `if (fp == 0) { throw ... }`.
The guard never fires. `FileIO.open` hands back a `File` wrapping a NULL `FILE*`,
and the failure surfaces later as a garbage read or a crash, at a site with no
connection to the missing file.

**Mechanism, from the emitted IR.** A `void*` return is tagged on the way out, and
the literal `0` it is compared against is tagged as an *int*:

```llvm
%t7  = call i8* @fopen(i8* %t4, i8* %t6)
%t8  = call i64 @__val_tag_ptr(i8* %t7)     ; NULL -> TAG_PTR|0
store i64 %t8, i64* %fp
%t9  = load i64, i64* %fp
%t10 = add i64 0, 0
%t11 = call i64 @__val_tag_int(i64 %t10)   ; 0    -> TAG_INT|0
%t12 = icmp eq i64 %t9, %t11               ; 0x7FF8.. vs 0x7FF9..  -> always false
```

`__val_tag_ptr` (`base_nanbox.ll:274`) masks to 48 bits and ORs in `TAG_PTR`, so a
NULL pointer becomes `0x7FF8000000000000`, not `0`. The comparison is between two
different tags and can never be true — for *any* NULL-returning `void*` extern,
not just `fopen`.

**Why it is easy to write and impossible to spot.** The declaration, the
annotation, and the guard are each individually right, and the C convention being
followed (NULL means failure) is the correct one. Nothing warns. The only way to
see it is to read the IR or to notice that the error path never runs.

**Workaround:** declare the extern as returning `i64` instead of `void*`
(`@extern("i64 fopen(void*, void*)")`). The value is then an untagged machine
integer and `== 0` works. This is what the `@http/server` binary-body work used
throughout, and it is why that code deliberately avoids `IO.open`.

**Fix.** Two candidates, and the choice is a real decision:

1. Compare against the untagged value: make a `== 0` test on a value of pointer
   provenance untag first. Narrow, but it needs codegen to track provenance,
   which it does not do.
2. Stop tagging `void*` extern returns and treat them as raw `Int` like every
   other extern result. This matches the existing rule that **all `extern`
   parameters must be untagged** (`CLAUDE.md`) — the return side is simply
   inconsistent with it. Then a NULL is `0` and every C-convention guard works.
   Audit needed: anything currently relying on a tagged `void*` return being a
   usable pointer value.

Option 2 is the systematic one and is consistent with the documented FFI
discipline; see `docs/design/ffi-pointer-discipline.md`.

Related in-family: `load8` returns a raw untagged i64 that reads back as a
denormal double unless laundered through integer arithmetic (`& 255`). Same root
theme — the extern boundary does not have one consistent tagging rule.

**Resolution (2026-07-31).** Took Fix A (the narrow one), not option 2, because
option 2 int-tags *every* void* return and several stdlib functions return a
void*-derived buffer through a `: String` signature and depend on TAG_PTR
(`string.sf` char_at/slice, `io.sf` File.read/read_line/read_all) — int-tagging
those would print numbers. New runtime helper `__val_tag_ptr_nullable`
(`base_nanbox.ll`, `wasm_base_32.ll`) maps a NULL to int-tagged 0
(`9221401712017801216`, the exact word `IntLit(0)` compares as) and leaves a
non-NULL pointer as normal TAG_PTR. The `i8*` extern-return path
(`intrinsics_body.sf`) now calls it via `emit_tag_ptr_nullable`
(`types_body.sf`, with an identity-mode ptrtoint fallback so the bootstrap never
references the symbol); the ~50 other `emit_tag_ptr` sites are untouched.
Verified: `IO.open` on an existing file opens, on a missing file throws; `test_file`
newly passes; zero regressions.

Still open, same family (guards against a NULL void* return that this fix's
mechanism now makes work, but the call sites weren't updated): `File.read_line`
EOF guard (`io.sf`), `string.sf` `_strstr(...) != 0` (inverted, though codegen
intercepts `contains` as a builtin so it's shadowed). Option 2 / `Ptr<T>`
(`docs/design/ffi-pointer-discipline.md`) remains the systematic long-term
answer and Fix A does not block it.

### 83. FIXED — `__float_to_string` bitcast an int-tagged value, so `.to_string()` on a whole-number Float printed `nan`

```saffron
var s: List<Float> = [10, 20, 30]
IO.println(s[1])                  // 20      — correct
IO.println(s[1].to_string())      // nan     — wrong
```

`__float_to_string` (`src/runtime/base_nanbox.ll:162`) opened with a bare
`bitcast i64 %v to double`. That is only valid if the incoming value really is
an unboxed double. Codegen picks this helper from the **static** type, and the
list is declared `List<Float>` — but `[10, 20, 30]` are whole numbers, so at
runtime the elements are int-tagged (`0x7FF9...`). Those bits *are* a NaN, hence
the literal output `nan`.

`IO.println(s[1])` looked fine only because it goes through a different helper
that dispatches on the runtime tag rather than the static type.

**Fix:** route through `__val_untag_float`, which already branches on the tag
and converts either shape. `base.ll` has the same bare bitcast but is the
bootstrap base and runs in identity mode, so it never sees a tagged value;
`wasm_base.ll` / `wasm_base_32.ll` have their own formatters, untouched.

Watch for the general shape: any runtime helper chosen by static type but fed a
tagged value must dispatch on the tag, not assume it. Same family as #77.

### 82. FIXED — a list index held in a `Float`-annotated variable always read element 0

```saffron
fun f() {
    var src: List<String> = ["a", "b", "c"]
    var i: Float = 0
    while (i < src.length()) { IO.println(src[i]) i = i + 1 }
}
f()      // printed "a a a"; with `var i = 0` it printed "a b c"
```

Silent wrong answers, no diagnostic, and it hit every container operation that
untags an index: `list[i]`, `list[i] = v`, `list.set(i, v)`, `str[i]`,
`str.char_at(i)`.

`__val_untag_int` (`src/runtime/base_nanbox.ll:221`) assumed a NaN-boxed input
and unconditionally masked the low 48 bits. A `Float`-annotated variable is
stored float-tagged (`__val_tag_float`), and `0.0`, `1.0`, `2.0` all have an
all-zero low 48 bits — so every index collapsed to `0` while the loop counter
itself incremented correctly. That split is what makes it so confusing to
debug: the counter prints right, the index is wrong.

**Why it stayed hidden.** The compiler uses `var i: Float = 0` as a list index
in hundreds of places, and none of them misbehave — the compiler compiles
*itself* in identity mode, where tag/untag are no-ops and the annotation has no
effect. The bug only reaches user programs, on the NaN-box path. Any bug in the
tag/untag helpers is invisible to the compiler's own test of itself; a
self-hosted compiler cannot dogfood its own value representation.

**Fix:** three input shapes reach the helper — NaN-boxed, raw i64 straight from
codegen, and a float-tagged double — so branch on which. `wasm_base_32.ll:685`
already did exactly this; native was the odd one out. The native version adds a
guard wasm32 lacks: a sign-extended negative raw int (high 32 bits all ones)
must pass through, since bitcasting it to double yields NaN and would return 0,
silently breaking negative indexing.

Suite: 43 failures vs. the 45-failure baseline, zero regressions, and
`test/narrowing.sf` + `test/test_async.sf` now pass. `./bootstrap.sh` passes.

### 81. FIXED — `__gc_write_barrier` was defined but never called anywhere, so the remembered set was permanently empty

`__gc_write_barrier` (`src/runtime/gc.ll:1378`) maintains the remembered set that
`__gc_minor_mark_roots` (`gc.ll:1520-1546`) depends on. It has exactly one
occurrence in the whole tree — its own definition:

```
grep -rn "__gc_write_barrier" src/   ->   src/runtime/gc.ll:1378 only
grep -rn "__gc_write_barrier" src/compiler/   ->   no matches
```

Codegen never emits it. (`runtime.sf` is compiled `--identity-mode`,
`bootstrap.sh:144`, and `build/stage3/runtime.ll` carries only a `declare` of
`__gc_push_root` with zero calls.) So any nursery object reachable *only* through
an old-generation object is invisible to a minor collection.

Reproduced: an old-gen list holding a nursery child, after one
`__gc_minor_collect()`, reads back `len=49` where 3 was stored, with garbage
values. A second repro isolates it as a **forwarding** failure specifically —
the shadow-stack root for the inner list is forwarded correctly, while the
`outer.data[0]` slot still points into the abandoned nursery:

```
root 'inner':       5637177368 -> [7, 8, 9]   (forwarded: true)
outer.data[0] slot: 5637177368 -> 5637177368  (forwarded: false)
```

Independent of #63: it still corrupts in a build where nursery space is never
reused, so it is not the stale-SSA-temp bug wearing a different hat.

**Fix**, three options in increasing order of appeal:
1. Emit `__gc_write_barrier` at every store of a pointer into a heap object
   (field-set, `list.push`, `map.set`) — broad codegen + `runtime.sf` change,
   medium-high risk.
2. Make minor GC scan the whole old generation for nursery references instead of
   relying on the remembered set — small and safe, but slower.
3. Retire the nursery, or make it non-moving. This removes #81 and #63's root
   cause together and leaves the shadow stack as the sole root mechanism, which
   is the only thing codegen actually maintains.

**FIXED** (option 2, `60ee39f` → `gc.ll`). `__gc_minor_collect` now walks the
whole old generation twice per minor collection: phase 1b (after `mark_roots`)
marks nursery objects referenced from any live old-gen slot, phase 3b (after
`update_refs`) rewrites those slots to the forwarded address. New helpers
`__gc_minor_scan_old_gen`, `__gc_minor_scan_old_object`, `__gc_minor_visit_slot`,
`__gc_minor_array_slots`; per-tag slot selection mirrors `__gc_mark_drain`
exactly, and lists visit `data_ptr` first so the forwarding pass reads the
already-updated array address. The write barrier and remembered set are left
intact — still correct, merely redundant while unpopulated.

Also fixed a pre-existing latent crash in `__gc_minor_mark_value` that the new
scan exposed. Nursery memory is recycled without zeroing (the bump pointer is
just reset), so a half-initialized object is observable with a nonzero count next
to a stale or null `data_ptr` — `__list_new` stores count/capacity, then calls
`__gc_alloc` for the data pointer, and that second allocation can trigger the
collection. `mark_value` validated only the nursery address range, so it
dereferenced that garbage; it now also checks the header magic sentinel, and the
`trace_list`/`trace_map` element walks are bounded by the target array's own
header size.

Regression test: `test/pass/gc_old_to_young.sf` (list child, string child, map
value). It segfaults against the pre-fix `gc.ll` and passes after — verified both
directions via `SAFFRON_RUNTIME_GC`.

**This is a real slowdown**, O(live old-gen objects) per minor collection, with
no fixed overhead — the entire cost is the old-gen size term. Forced-collection
worst case (2010 old-gen objects × 2000 minor collections, no old→young edges):
0.00–0.01s → 0.19–0.23s. A 400k push loop: roughly 2×. A transient-allocation
loop with a small old gen is unchanged. Option 3 remains the better end state and
would remove this cost along with #63.

**Option 3 has since been taken** — the nursery is retired (see #63), so there is
no young generation, no old→young edge, and this entry's remembered set has
nothing to remember. The option-2 old-gen scan and its O(live old-gen) cost are
therefore dead code too: `__gc_minor_collect` is never reached. Both remain in
the tree for whoever revives the nursery, along with the write barrier they were
built around.

### 80. FIXED — a name bound by both a `fun` and a `var` compiled to a call through the variable's value

**Only one of the three tests was a live defect.** `comprehensive.sf` and
`nullable_narrowing.sf` were fixed by other landed work and now pass at rc=0
under `tools/gc_stress.sh` at a 4KB nursery — the harness that originally flagged
them. Filing all three together pointed at three causes where there was one.

`functions.sf` died on `0x7ff9000000000001` — TAG_INT with payload 1, i.e. the
NaN-boxed integer `1` being unpacked as a closure-pair pointer. That address is
the whole bug: nothing rejects a name claimed by both a `fun` and a `var`, and
codegen had no rule for which wins, so a call on such a name loaded the
*variable* and jumped through its value.

Two shapes. A nested `fun` returned by name compiled `return cl` to
`load i64* @__g_cl` — the module global, read during its own initializer and so
still 0, giving a null closure pair. A top-level `var a = 1` next to `fun a()`
fell past the direct-call branch (which correctly declines when a binding exists,
because the binding might hold a lambda) and read `a`'s value as a closure pair.

Fixed by giving a nested `fun` the innermost-wins binding that `var` already had
from `current_fn_locals` (#59), recorded under a reserved `__nestedfn:` key so
the alloca and GC-root sites sharing that map cannot mistake it for a `var`, and
consulted *above* the `module_globals` guards; plus dispatching directly when the
callee's recorded type is concretely non-callable. `Fun(...)` and `Any`
deliberately stay indirect — that is what keeps lambdas working through untyped
parameters. Regression test:
`test/pass/segv_name_collision_fun_vs_var.sf`, 13 assertions on returned
*values*, plus controls that must not change.

**`functions.sf` no longer segfaults but now hangs**, which is the file's own
property rather than a remaining codegen defect: its last section is
`fun c() { c("too","many") }`, unconditional infinite recursion written to
exercise a diagnostic codegen does not emit. Over-applying a 0-arity function is
silently accepted — `fun c(){}; c("too","many")` compiles and runs. The segfault
was masking that. Worth its own entry.

### 80-orig. Three tests in `test/` segfault with the garbage collector fully disabled — non-GC codegen faults hiding behind the GC bugs

`test/comprehensive.sf`, `test/functions.sf` and `test/nullable_narrowing.sf` all
exit 139 under `tools/saffron run`, and they still exit 139 when linked against a
GC whose collector is a no-op. The control that makes this conclusive: `#63`'s
three-line repro (`m1.sf`) prints `len=20000` correctly in that same
GC-disabled build, so the harness does suppress real GC faults — these three are
something else.

Found by sweeping the suite with a tiny-nursery stress harness, now checked in as
`tools/gc_stress.sh`. It flagged five files; the other two were genuine #63
instances. These three are **not** filed as GC defects and want separate
diagnosis. They are also a live contributor to the suite's failure count, so they
should not be attributed to GC work.

**Also nondeterministic: `test/gc_deep_test.sf`.** It segfaults intermittently
rather than every run — measured 1/25 on one tree and 4/25 on another built from
the same commit. Like `tuple_test` before #73, that makes it a phantom in any
baseline failure-set diff: it appears on one side and not the other and reads as
caused by whatever is under test. The check that settles it is comparing the
*generated IR*, not the run: two trees whose `saffronc` differ only in the parser
and checker emit byte-identical IR for this file, so a crash difference cannot be
attributable to that change. Discount it in failure-set diffs the same way, and
prefer an IR diff over re-running when a segfault flips.

The harness is the reusable artifact from the GC dive, because it converts these
latent hazards into deterministic crashes. Reproduce the split above with it
directly — a 4KB nursery collects almost continuously, a 1GB one never collects at
all, so a failure that survives both is not a GC failure:

```bash
tools/gc_stress.sh test/comprehensive.sf                  # rc=139
NURSERY=1073741824 tools/gc_stress.sh test/comprehensive.sf   # rc=139 -- not the GC
tools/gc_stress.sh m1.sf                  # len=19999  -- one element lost
NURSERY=1073741824 tools/gc_stress.sh m1.sf   # len=20000  -- clean, so it IS the GC
```

### 79. FIXED — coroutines never popped their GC roots, so the shadow stack grew without bound and its top slots dangled into freed frames

`gen_fun`'s coroutine epilogue (`src/compiler/codegen/output_body.sf:413-438`) emits
`llvm.coro.free` + `llvm.coro.end` but no `__gc_pop_roots`, and `gen_return` skips
the pop on its `is_coroutine` branch (`src/compiler/codegen/stmts_body.sf:279-282`)
where the non-coroutine branches at `:286` and `:293` do pop.

Measured unbounded growth over spawn/await rounds:

```
depth before any spawn = 14
after 50 rounds  = 116
after 150 rounds = 316
after 200 rounds = 414
```

A second repro shows a leaked top slot pointing into a `__sf_malloc`'d coroutine
frame that `llvm.coro.free` has already released, so mark dereferences freed memory
on every subsequent collection.

**Not demonstrated as a crash**: 400 rounds with a forced `__gc_collect()` each
survived, because the freed frame tends to stay mapped. So this is a confirmed
unbounded leak plus a real read-after-free, but unproven as a fault — ranked below
#63 and #81 for that reason.

**Fix.** Emit `__gc_pop_roots(n)` in `__coro_final` before `coro.end`, and on the
coroutine return path. Small and low-risk.

**FIXED**, and the fix needed one thing this entry did not anticipate: the pop has
to be **guarded**, not unconditional. The shadow stack is a single global LIFO
while coroutine lifetimes are *not* nested — a task spawned later can still be
live, with its roots stacked above ours, when we finish. Popping a fixed count
there discards *its* roots and collects objects still in use, trading a leak for
a use-after-free. So `__coro_final` records the depth at entry, recomputes the
expected top, and pops only when our region is provably still the top; otherwise
it leaves the roots, because leaking a root is recoverable and freeing a live one
is not (`output_body.sf:579-591`).

Re-measured 2026-08-02 against the same repro shape — depth is flat, not growing:

```
depth before any spawn = 13
after 50 rounds  = 13
after 200 rounds = 13     (was 414)
```

### 78. FIXED — an absolute import path silently resolved to a nonexistent stdlib file

`import "/abs/path/m.sf" as M` does not import anything. `resolve_import_path`
(`src/compiler/main.sf:109`) tests for `@`, `./`, `../`, and bare paths with and
without a slash — there is **no branch for a path starting with `/`**. An
absolute path contains a slash, so it takes the "package submodule" branch,
fails to find anything, and falls through to `_find_in_lib_paths`, whose last
line is an unconditional

```saffron
return stdlib_dir + "/" + name + ".sf"
```

That path does not exist, and nothing downstream checks: `collect_modules` is
called with it regardless (`main.sf:591`). The module's declarations are never
registered, so every reference through the alias breaks — in three different
ways depending on what you touch.

**Reproduction.** Identical file, identical code, only the import spelling
differs:

```saffron
// /tmp/mtC/m.sf
enum S { Return(value: Int), Other }
```

```saffron
import "./m.sf" as M            // works, prints 5
import "/tmp/mtC/m.sf" as M     // fails
fun f(s: M.S): Int { return match (s) { Return(v) => v, _ => 0 } }
IO.println(f(M.S.Return(5)))
```

Three distinct symptoms from the same cause, none of which names the import:

| construct | symptom under an absolute import |
|---|---|
| `match` binding a variant field | `[codegen] Error: undefined variable 'v'` |
| reading a module global | invalid IR: `load i64, i64* %M` |
| calling a module function | `linker command failed` (undefined symbol) |

The `undefined variable 'v'` case is the nastiest: the name it reports is the
*pattern binding*, which is not the problem and is not even a variable the user
declared. Nothing points at the import.

**Why this cost me time, and why it is worth filing rather than shrugging at.**
Absolute paths are what you reach for when driving a compiler pass from a
scratch file outside the tree, which is exactly what testing a new pass looks
like. The failure presents as "my new AST variant broke variant bindings" — I
bisected `ast.sf` by truncation before noticing the truncated file worked fine
under a *relative* import, at which point the file was exonerated entirely.

**Fix.** Two independent parts, and the second matters more than the first:

1. Add an `import_path.starts_with("/")` branch to `resolve_import_path` that
   returns the path unchanged.
2. Make an unresolvable import a hard error. `_find_in_lib_paths` returning a
   guessed path that no one stats is the actual defect — with `/` handled, the
   next unsupported spelling fails exactly as opaquely. Check
   `OS.file_exists(full_path)` at the `collect_modules` call sites and report the
   import path and the spelling that was tried.

Part 2 is the same shape as several entries here: a resolution helper that
cannot fail, so it returns a plausible wrong answer instead. Compare #22 and
#40, where a lookup that should have said "not found" said "assume it's a
local."

**FIXED, both parts.** The `starts_with("/")` branch returns the path unchanged,
and the `OS.file_exists(full_path)` guard reports the import path, the importing
file, and the path that was tried before `c_exit(1)`. The repro above now prints
`hi from abs`, and a bogus `@definitely_not_a_module` says so by name instead of
failing at the linker.

The guard had to go in **twice**: the import-resolution loop is duplicated
between `collect_modules` and the entry module's own copy, and patching only the
first left `@definitely_not_a_module` silent from the entry file. A third copy
handles package dependency preludes and is still unguarded — it is reached only
for `<lib>/<pkg>/src/prelude.sf` imports. Three near-identical copies of this
loop is the underlying code smell; see the duplication notes in this file.

Part 2 turns two tests from a link-error into a compile-error, which is the point
of it: `enum_cross_simple` imports `./enum_module_helper.sf`, a file that has
never existed in this repo, and `test_package_import` imports `testpkg`, which
lives at `test/testpkg` and is not on any `--lib-path` that `run_tests.sh`
passes. Both were already failing; they now fail naming the import.

### 77. FIXED — on wasm32 only, `true.to_string()` returned `"false"`; `__bool_to_string` untagged a value codegen had already untagged

Branching on the same value is correct, which is what makes this so misleading: an
`if` takes the right arm while `to_string()` on the identical variable prints the
opposite. It cost real debugging time during the playground work by making a
correctly-functioning `Map` look broken.

```saffron
var t: Bool = true
IO.println(t.to_string())        // "false" on wasm32, "true" on native
if (t) { IO.println("taken") }   // "taken" — correct on both
```

Reproduced by building for wasm32 and running the module under Node: output is
`false`, `false`, `taken`. The same program on native prints `true`, `false`,
`taken`.

**Cause — a double untag, and `wasm_base_32.ll` is the only base that does it.**
Codegen untags before the call (`src/compiler/codegen/methods_body.sf:2426`):

```saffron
var raw: String = this.emit_untag_bool(obj)
this.emit_indent(local + " = call i64 @__bool_to_string(i64 " + raw + ")")
```

so `__bool_to_string` receives a plain 0/1. Three of the four IR bases test that
directly with `icmp ne i64 %b, 0` — `base.ll:125`, `base_nanbox.ll:132`,
`wasm_base.ll:709`. But `wasm_base_32.ll:912` untags a *second* time:

```llvm
%raw = call i64 @__val_untag_bool(i64 %b)
%is_true = icmp ne i64 %raw, 0
```

and `__val_untag_bool` (`wasm_base_32.ll:788-793`) is `icmp eq i64 %v, 9221683186994511873`
— a comparison against the fully tagged `true` pattern. Given the already-reduced
`1` it yields 0, so every `true` formats as `"false"`. `false` also formats as
`"false"`, which is why the bug hides: half the cases look right by accident.

The fix is to drop the extra `__val_untag_bool` call so wasm32 matches the other
three bases. This is exactly the hazard `CLAUDE.md` states as a rule — tagging and
untagging happen in one place, "never in codegen, and never in both" — so it is
worth checking the other `wasm_base_32.ll` `to_string` helpers for the same
duplicated-untag shape rather than fixing only this one.

**FIXED.** The extra call is gone and the parameter now carries a comment stating
the convention it is part of — `%b` arrives already untagged, which is what the
other three bases encode by testing `icmp ne i64 %b, 0` directly. Verified in
both directions by swapping `wasm_base_32.ll` and rebuilding: the pre-fix module
prints `false, false, taken` under Node, the fixed one prints
`true, false, taken`.

**The audit this entry called for found a worse bug, and it was not in this
file.** No other `wasm_base_32.ll` `to_string` helper untags twice —
`__io_println_bool` looked like a candidate but is declared and never called from
anywhere, so it has no convention to be wrong about. The same *shape* did turn
up in codegen, on **native**, and much larger: `emit_enum_to_string` handed
`__bool_to_string` a still-tagged value (untagging zero times, the mirror of this
bug's twice), appended a tagged String straight into `strlen`, and had no `Float`
arm at all. See #102. Worth generalising from: "which of the four bases is the
odd one out" is the narrow question, and "does every caller of this helper agree
on the convention" is the one that finds more.

### 76. FIXED — the type checker never descended into class method bodies, so every check was silently skipped inside a method

`check_stmt`'s `ClassDecl` arm (`src/compiler/checker.sf:1023`) registers the
class's fields and parent and then stops. It never walks `methods`. So **no
statement inside any class method is type-checked at all** — not the nullability
analysis the checker exists for, not scalar mismatches, not return types, not
exhaustiveness, not undefined variables.

```saffron
class K {
    fun init() {}
    fun m(): Int {
        var s: String = 1       // not reported
        var n: Int = nil        // not reported
        return "not an int"     // not reported
    }
}
```

Compiles silently. The identical statements in a *free function* are all caught:

```
ERROR: s: cannot assign Int to String
ERROR: n: cannot assign nil to non-nullable Int
```

That contrast is the whole bug: the checks work, they are just never reached.
Same for exhaustiveness — a `match` missing a variant inside a method is
accepted, while the same match in a free function is rejected with
`non-exhaustive match on S: missing variants C, D`. Verified with both a local
enum and one imported across a module boundary; the module boundary is
irrelevant, the class boundary is what matters.

Only codegen's own late `undefined variable` check (`expr_body.sf:97`) still
fires inside methods, which is why this hasn't been more visible: the loudest
class of mistake is caught by a different pass, one that reports at codegen time
without a source span.

**Scale.** This is most of the language's surface in practice, and nearly all of
the compiler's own source: `checker.sf` has 126 methods and 4 top-level
functions, so ~97% of its own bodies are in the blind spot. The compiler
bootstraps *because* it is unchecked, not because it is clean — running the
checker over its own source currently reports zero diagnostics, which should be
read as "no checks ran," not "no problems."

**Fix.** Recurse into `methods` from the `ClassDecl` arm, with `current_class`
set and a scope holding `this` plus the field names. Expect this to surface a
large pile of latent diagnostics in the compiler and stdlib — that is the point,
and it is the same "measure first" situation as stage 1 of
`docs/design/compiler-rewrite.md`, which should probably absorb this. Do not
land the recursion and the resulting fixes in one commit.

**Measured.** The recursion itself is four lines — `current_class` (declared at
`checker.sf:162`, never assigned anywhere) set around a `check_stmts(methods)`,
since the `FunDecl` arm already handles a method correctly once `current_class`
is set, and `this.field` resolves via `class_fields` rather than lexical scope so
no extra scope work is needed. Applied experimentally, gen3 then **rejects the
compiler's own source**: 28 errors in `parser.sf`, 35 in `checker.sf`, 40 in the
assembled `codegen.sf`, plus `main.sf`. Nearly all are non-exhaustive matches of
the one-armed form `match (expr) { Variable(n) => n }` (e.g.
`checker.sf:2210`) — no `_`, so every other variant yields an indeterminate
value. That is precisely the mechanism of #73, sitting in ~100 places.

The measurement was only possible by hand because `bootstrap.sh` did **not**
verify gen3 can compile itself, contra CLAUDE.md's promotion criteria: its TEST
stage compiled only `test/hello_bootstrap.sf`, and stage 1 uses gen2, so a gen3
that rejects the compiler source still gave a green bootstrap.

**Fixed as of 2026-07-31**: `bootstrap.sh` gained a stage 2 that compiles the
compiler's own source with gen3, links gen4, and checks gen4 can compile a
program. It found one real defect immediately — `codegen.sf:574` was a one-armed
`match (stmts[si2]) { VarDecl(n, t, i, d) => n }` in a *free function*, so gen3
already rejected it today, with no relation to the #76 recursion. Replaced with
the existing `gen.get_var_name()` helper, which has the `_ => ""` fallback (the
line below it already used `gen.get_var_doc()`). With that, gen3 → gen4 → probe
passes clean.

So the ~103 count above splits: **one** site blocks gen4 today, and the other
~102 are inside class method bodies, invisible until the recursion lands. Stage 2
will now catch each one as it is uncovered, instead of the whole pile arriving at
once with no way to tell a real regression from a pre-existing hole.

Note the related shape: the `match (stmt)` at `checker.sf:837-848` and
`check_stmt` itself both cover 14 of `Stmt`'s 15 variants — `TypeAlias` has no
arm in either — and this does not fail the build, because exhaustiveness
checking is one of the things that does not run inside a method. The checker's
own blind spot hides a hole in the checker.

**FIXED** — recursion in `a902110`, the ~100 diagnostics it uncovers in
`3138807` (landed first, so the recursion commit keeps the bootstrap green). The
`ClassDecl` arm now saves `current_class`, sets it to the class name, runs
`check_stmts(methods)` and restores it; the `FunDecl` arm already did the right
thing under that flag, and `this.field` resolves through `class_fields` rather
than lexical scope, so no extra scope push was needed. Four lines, exactly as
measured above.

Two things the measurement got right and one it got wrong. Right: the count
(~100) and the dominant shape (one-armed `match` with no `_`, i.e. #73's
mechanism sitting in ~100 places). Wrong: the split into "one site blocks gen4,
~102 invisible" implied the ~102 were all mechanical fallbacks. Four were
genuine defects that the recursion found and nothing else would have —
`collect_calls_from_expr`/`_stmt` in `utils_body.sf` were missing recursive
cases outright, so whole subtrees of the call graph went uncollected. That is
the argument for this entry: the value was never the mechanical `_ => ...`
arms, it was the handful of real holes they were camouflaging.

Now that the checker runs inside methods, the compiler's own source is checked
for the first time. Treat a fresh diagnostic in compiler source as real: there
is no longer a "the checker doesn't look here" explanation available.

### 74. FIXED — builtin-namespace calls (`GC.*`) passed NaN-boxed arguments straight into `@extern` functions, and the matching getter re-masked the corruption so it read back correct

Found while wiring `GC.set_max_memory()` for the `--max-memory` work. It is not
specific to the new function — `GC.set_threshold()` has always had it — and it is
a *dispatch-path* bug, so the same `@extern` declaration behaves differently
depending only on what the module was aliased to at the import site.

`IO`, `OS` and `GC` are registered with an *empty* module prefix
(`src/compiler/codegen.sf:519-521`), so `GC.foo(x)` does not go through the
universal module dispatch in `src/compiler/codegen/methods_body.sf:955`. It falls
through to a hardcoded block that rewrites the callee to the underlying runtime
symbol (`_set_threshold` → `@__gc_set_threshold`) and emits the argument
**still NaN-boxed**:

```llvm
  %t1 = add i64 0, 4096
  %t2 = call i64 @__val_tag_int(i64 %t1)      ; tag it
  %t3 = call i64 @__gc_set_threshold(i64 %t2) ; ...and pass the tagged value
```

`4096` arrives at the runtime as `9221401712017805312` (≈9.2 EB). The
`stdlib_gc_set_threshold` wrapper that `src/lib/gc.sf` actually declares — which
*does* untag correctly — is emitted into the module but never called:

```llvm
define linkonce_odr i64 @stdlib_gc_set_threshold(i64 %bytes.arg) {
  %t2 = call i64 @__val_untag_int(i64 %t1)   ; the correct untag
  call void @__gc_set_threshold(i64 %t2)     ; nothing calls this wrapper
}
```

**Why it has gone unnoticed: the getter conceals it.** Both directions are
consistently wrong, so a set/get round-trip returns the original value.
`__gc_stat_threshold` returns the corrupted global raw, and the interpolation path
then applies `__val_untag_int`, which masks off exactly the 16 tag bits that
`__val_tag_int` had OR-ed in. The value that was never stored correctly is
reconstructed on the way out:

```saffron
GC.set_threshold(4096)
IO.println(GC.threshold())   // prints 4096 — but the global holds 9.2e18
```

Any consumer that reads the global *without* that round-trip sees the real value.
`@__gc_alloc`'s `icmp uge i64 %total, %thresh` (`src/runtime/gc.ll:335`) is such a
consumer, so `GC.set_threshold()` silently does nothing to auto-collection: the
threshold is effectively infinite and the compare never fires.

**Reproduction** — the alias is the only difference:

```saffron
import "@gc" as GC        // hits the hardcoded path: BROKEN
import "@gc" as Memory    // hits universal module dispatch: CORRECT
```

With `--max-memory` the divergence is directly observable, because the cap has a
consumer that is not a getter:

| program | result |
|---|---|
| `import "@gc" as GC` + `GC.set_max_memory(16m)` then overflow | `rc=0`, cap never enforced |
| `import "@gc" as Memory` + `Memory.set_max_memory(16m)` then overflow | `rc=3`, cap enforced |

There is also a return-type mismatch on the same emission path: the callee is
declared `declare void @__gc_set_threshold(i64)` but called as
`%t3 = call i64 @__gc_set_threshold(...)`. `llvm-as` accepts the module, so this
is latent rather than fatal today.

**Related but distinct from #24.** #24 is about `gen_extern_call` passing `i64`
params through raw in the *normal* extern path. This is the builtin-namespace
dispatch path bypassing the generated `stdlib_*` wrapper altogether, which is why
aliasing the same module to a different name fixes it. Fixing #24 does not fix
this.

**Fix direction:** make the empty-prefix builtin path route through the generated
`stdlib_gc_*` wrapper like every other module, rather than rewriting the callee to
the raw runtime symbol. Failing that, untag arguments at the rewrite site — but
then the getters must stop double-masking, or the round-trip starts reporting
wrong values instead of right ones by accident.

**Not fixed here.** `--max-memory` and `SAFFRON_MAX_MEMORY` set the cap from the
runtime constructor and do not go through this path, so they are unaffected. Only
the `GC.set_max_memory()` / `GC.set_threshold()` Saffron-level setters are.

**FIXED** — the first fix direction, and it is the same one-line shape #88 used
for `IO`/`OS`. `GC`'s hardcoded prefix registration is now guarded by
`if (!alias_map.has("GC"))`, so an actual `import "@gc" as GC` wins and the call
routes through universal module dispatch to `stdlib_gc_set_threshold`, which
untags properly. The guard goes on all three occurrences in `codegen.sf`. The IR
for `GC.set_threshold(4096)` now shows
`%t2 = call i64 @__val_untag_int(i64 %t1)` followed by
`call void @__gc_set_threshold(i64 %t2)` — the wrapper is reached, and the raw
runtime symbol is no longer called with a boxed argument.

The getter's compensating double-mask is untouched and still correct: it masks a
value that is now genuinely tagged, rather than one that was corrupt.

The `declare void` / `call i64` return-type mismatch on the old rewrite path was
not separately fixed — that path is simply no longer taken for an aliased `GC`.
It still applies to any builtin namespace used without an import.

### 73. `tuple_test` compiles or fails at random — the same binary on the same source disagrees with itself run to run — FIXED

Found while diffing a baseline failure set for #69, where it presented as a
phantom regression. `test/tuple_test.sf` either compiles and runs, or fails in
the checker with:

```
ERROR: return: cannot return nullable Int from function expecting Tuple<Int,Int>
```

Measured on a pristine tree: **10 failures out of 20 runs** of the *same*
`build/saffronc` binary against the *same* unmodified source file. A second
build of identical source gave 18/20. Nothing about the input varies.

The error text comes from `checker.sf:988`, in the `Return` arm:

```saffron
if (!this.is_nullable_type(this.current_func_ret) and this.is_nullable_type(val_type_node)) {
```

so whether `is_nullable_type` sees a Tuple return annotation as nullable is
apparently not a function of the source alone. That points at uninitialized
memory or pointer-identity-dependent behaviour in the type node built by
`parse_type_node`, rather than at tuple support as such.

**Why it matters beyond this one test.** It poisons any baseline comparison. A
failure-set diff against a same-HEAD baseline will show `tuple_test` flipping in
either direction, which reads as a regression or an improvement caused by
whatever change is under test. Anyone measuring against the suite has to know to
discount it. Either fix it or quarantine the test, but it should not stay as
silent noise in the suite.

**Fixed.** Not uninitialized memory in `parse_type_node` after all — the guess
above was wrong. `infer_type`'s match over `AST.Expr` (`checker.sf:1281`) simply
had **no arm for `TupleLit`**, and none for `Yield` either. Saffron's `match` does
not trap on an unmatched variant; it yields whatever is in the result slot. So
`return (b, a)` got a leftover value as its type, which `is_nullable_type` then
read as nullable on about half of all runs. Nothing varied about the source; what
varied was the contents of that unwritten result slot, which is why a second build
of identical source gave a different failure rate.

The fix adds both arms. `TupleLit` infers each element and returns
`GenericType("Tuple", elem_types)`, mirroring the `Tuple<A,B>` spelling the parser
already produces in `parse_single_type`; `Yield` infers its operand and answers
`AnyType`, since a `yield`'s own value is not usable. `tuple_test` now passes
20/20.

The generalizable part is the failure mode, not the two arms: **a non-exhaustive
match in Saffron is a silent indeterminate value, not an error.** That is why #76
matters so much — the checker's exhaustiveness diagnostic is the only thing
standing between this class of bug and a nondeterministic compiler, and #76 means
it never ran on any method body, including these.

### 72. An `Int?` return annotation makes the function's own parameters undefined at codegen — FIXED

`Int?` and `Int|Nil` are meant to be the same type, but only one of them keeps
the parameter list intact:

```saffron
fun f(x: Int): Int? {
    return x
}
IO.println(f(5))
```

```
[codegen] Error: undefined variable 'x'
Compilation failed with codegen errors
```

The identical body compiles and prints `5` under either other spelling of the
return type:

| Return annotation | Result |
|---|---|
| `: Int` | compiles, prints 5 |
| `: Int\|Nil` | compiles, prints 5 |
| `: Int?` | **`undefined variable 'x'`** |

So it is the `?` spelling of the *return* annotation that loses the parameter
scope — the parameter itself is unremarkable. This one at least fails loudly.

Note that `Int?` is also rejected outright in some other positions
(`var v: Int? = 5` is a parse error, "expected '=' but found '?'"), so the
nullable shorthand is only partly wired up; this entry is specifically the
codegen scope loss, which is the surprising half.

**Fixed.** The two symptoms are one cause, and it is not in codegen: **`parse_type`
never consumed the `?` token at all.** It left the `?` for whatever came next to
trip over. In a return annotation that desynchronized the parser, and the
function's own parameters came out undefined by the time codegen ran; in a `var`
annotation the same unconsumed token surfaced immediately as "expected '=' but
found '?'".

`parse_type`'s comment claimed the `?` was "preserved as-is" for the checker and
codegen to interpret, which was only ever half true: the checker had exactly two
`ends_with("?")` special cases, codegen had none, and `parse_type_ast` would have
built `ClassType("Int?")`. `NullableType` is declared at `ast.sf:13` and
**constructed nowhere**. So a nullable had no single representation.

Rather than add a `?` case to each consumer, `parse_type` now desugars `T?` to
`T|Nil` on the spot — including on union members, so `T?|U` and a trailing `?`
after a member both work. Downstream there is exactly one spelling of a nullable,
and it is the union form every consumer already handled. This is the same
whitelist-versus-one-representation lesson as #69: the bug was N places each
needing to know about a special form.

`var v: Int? = 5` now compiles too, as a side effect rather than a separate fix.
One consequence worth noting: `Int?` being genuinely `Int|Nil` means it now
correctly *requires* a nil check, so `IO.println("${v}")` on an `Int?` is a
diagnostic where the old broken spelling would have been a crash.

### 71. FIXED — a module-level `fun main` in an imported file collided with the entry point's `main`, and the program silently never ran

Codegen renamed *every* function called `main` to `__saffron_main`, regardless of
which module it came from (`src/compiler/codegen/output_body.sf:3`), and the call
site rewrote *any* call spelled `main(...)` to `__saffron_main` based on the
pre-resolution name (`src/compiler/codegen/expr_body.sf:2091`). So when an
imported module defines `main` — as `turmeric/src/prelude.sf:1125` does, where
`main` is the `<main>` element builder — the two collapsed onto one symbol.

The failure is entirely silent. No diagnostic, exit code 0. The generated entry
point called the *library's* `main` with the wrong arity, and the user's `main`
was emitted but never referenced:

```
define i64 @__saffron_main(i64 %label.arg) {   ; the LIBRARY's main
...
  %t1 = call i64 @__saffron_main()             ; entry calls it with 0 args
```

Because the user's `main` was unreachable, `-O2` then stripped everything it
alone reached — which for the playground frontend meant the `js_fetch_post` and
`js_run_wasm` imports vanished from the linked module (7 imports instead of 16).
That surfaced as "the Run button does nothing", and I originally misdiagnosed it
as the wasm32 export list being applied after `-O2` stripped callback-reachable
externs. It is not an export-list problem at all.

**Fixed** in both places: the rename now applies only when `current_prefix` is
empty (i.e. only the entry module's `main`), and the call-site redirect now
tests `resolved_callee == "main"` rather than the unresolved `callee_name`.
Verified: the frontend's entry point now calls its own function, and the linked
wasm32 module carries all 16 expected imports.

One related case is still open: with a *named* import (`import { main } from
"./lib.sf"`) plus a local `fun main`, the program segfaults. That is the general
named-import-shadows-a-local problem, not this rename bug, and is not fixed here.

### 70. FIXED — `super` was completely broken; every use emitted `load i64, i64* %super` and the module failed to verify

**FIXED for calls** (`super.method(args)`) by hoisting the `super` arm above the
builtin-dispatch preamble, as the "Fix" section below prescribes. **Still broken
for `super` in value position** (`IO.println(super.f)` — a bare field/method
reference with no call); see the addendum at the end of this entry.

`super` does not work at all. Not an edge case, not a deep-inheritance problem:
every spelling fails, and the failure is at LLVM verification, so nothing runs.

```saffron
class A {
    var n: Int
    fun init(n: Int) { this.n = n }
    fun f(): Int { return this.n }
}
class B extends A {
    fun f(): Int { return super.f() + 1 }
}
IO.println(B(1).f())
```

```
saffron: the compiler emitted invalid LLVM IR
saffron: this is a compiler bug, not an error in your program.
  opt: output.ll:227:24: error: use of undefined value '%super'
    %t1 = load i64, i64* %super
```

Verified identical for all three forms: an overriding method calling
`super.f()`, `super.init(n)` in a subclass constructor, and a two-level chain
(`LoudDog extends Dog extends Animal`). This is why
`docs/learnxinyminutes/learnsaffron.sf` still does not compile after its parse
errors were fixed — §9 uses `super.speak()`.

Pre-existing, not a regression: reproduces identically on a compiler built from
`11c9638` (this session's starting commit).

**Cause — a dead load emitted 880 lines before the code that handles `super`.**
The `super` path itself is *correct*. `gen_method_call` has a dedicated arm at
`src/compiler/codegen/methods_body.sf:2520` that loads `%self` and calls the
parent's mangled symbol directly:

```saffron
if (obj_name == "super" and this.current_parent.length() > 0) {
    var self_val: String = this.fresh_local()
    this.emit_indent(self_val + " = load i64, i64* %self")
    ...
    var parent_method: String = this.current_parent + "__" + method
```

And it does run — the emitted IR contains the right call:

```llvm
define i64 @B__f(i64 %self.arg) {
entry:
  %self = alloca i64
  store i64 %self.arg, i64* %self
  %t1 = load i64, i64* %super      ; <-- garbage, nothing defines %super
  %t2 = load i64, i64* %self
  %t3 = call i64 @A__f(i64 %t2)    ; <-- correct super dispatch
```

The problem is the *builtin dispatch* preamble at
`src/compiler/codegen/methods_body.sf:1640`, which unconditionally evaluates the
receiver on the way to deciding what kind of call this is:

```saffron
// --- Builtin dispatch (only for non-class objects) ---
var obj: String = this.gen_arg_value(object)
```

`object` here is `AST.Expr.Variable("super")` (`src/compiler/parser.sf:595-597`
parses `super` into an ordinary `Variable` node). `gen_arg_value` has no idea
`super` is special, so it emits the load for a variable slot that no function
prologue ever allocates. Execution then falls through to `:2520`, which emits the
correct call — leaving both in the output. The dead load is unused, so this is
purely a verifier failure, not a miscompile: `%t1` is never read.

Note the guard at `:2520` is `obj_name == "super" and this.current_parent.length()
> 0`, and `current_parent` is evidently non-empty here since the correct call is
emitted. So `current_parent` tracking is fine; only the ordering is wrong.

**Fix.** Hoist the `super` arm above the builtin-dispatch preamble, so it is
reached before anything evaluates the receiver. The `this`-receiver case at
`:877-881` is already handled this way — `classify_expr(object) == "this"` is
tested at the very top of `gen_method_call`, precisely so `%self` is loaded
deliberately rather than by falling into generic variable codegen. `super`
deserves the same treatment and does not have it.

More generally this is the same shape as several entries here: an unconditional
`gen_arg_value` in a dispatch chain emits IR for a case a later arm was going to
handle differently. Emitting side-effecting IR before the dispatch decision is
made is the underlying hazard, and it argues for resolving receiver kind in a
separate pass (`docs/design/compiler-rewrite.md`, stage 2) rather than
re-deriving it inline at each of a dozen call sites.

**Addendum — fixed for calls, still open for `super` in value position.**
The arm was moved from `:2520` to immediately after the `this` arm (now
`methods_body.sf:883`), above the `:1640` preamble. Locals were renamed
(`super_self`, `super_args`, …) to avoid colliding with the enclosing scope at
the new location. All three forms in the repro now run correctly, plus a
three-level chain `C extends B extends A` returning `ABC`:

```
super.f() + 1        → 2
super.init(n)        → 5
two-level speak()    → Woof!!
three-level f()      → ABC / AB / A
```

`docs/learnxinyminutes/learnsaffron.sf` now compiles (rc=0, warnings only). It
still segfaults at runtime, on something unrelated to `super` — not yet
diagnosed. Full suite: 100 passed / 44 failed, zero regressions against the
45-failure baseline.

What is *not* fixed is `super` used as a value rather than a callee:

```saffron
class B extends A {
    fun g() { IO.println(super.f) }   // no call parens
}
```

still emits `%t1 = load i64, i64* %super` and fails verification. That path
never reaches `gen_method_call` at all — it goes through field/property access,
which has its own unconditional receiver evaluation and no `super` arm to hoist.
`test/inheritance.sf:20` contains exactly this line, which is why that test is
still red — though note its *first* error is unrelated (`var b` is declared
twice at top level, so the module has a duplicate `@__g_b` global).

Whether bare `super.f` should even be legal is a language-design question: it
would have to mean a parent-bound method value, and Saffron has no other way to
spell one. Rejecting it in the checker with a clear diagnostic is likely better
than making it work.

**FIXED — the value position too, and more cheaply than the addendum expected.**
No new dispatch arm was needed. `super` and `this` name the *same* receiver
pointer; only the dispatch differs, and `gen_method_call` already handles that.
So the variable-load path just has to know that `super`'s storage is `%self`:

```saffron
if (name == "super") {
    var_ref = "%self"
}
```

Two subtleties made this a three-attempt fix rather than a one-line one. The
alloca is `%self`, not `%this` — the prologue emits
`store i64 %self.arg, i64* %self`. And the variable-load path is **duplicated**:
the `Variable` arm in `expr_body.sf` and a second copy inside `gen_arg_value`.
Patching only the first left the error in place, because the failing site was the
`:1640` preamble's `gen_arg_value` call. Both copies now carry the arm.

`super.f` in value position yields the receiver, and the checker still rejects
anything that would misuse it. `test/inheritance.sf`'s remaining failure is the
unrelated duplicate `var b` at top level noted above.

### 69. `is` always returns false on a union-typed value, so every nil-check branch on `T|Nil` is silently dead — FIXED

Found while writing the Map-iteration workaround for #62. On a value whose
declared type is a union, **both** arms of an `is` test are false:

```saffron
var v: Int|Nil = 5
IO.println(v is Int)   // false   <-- wrong
IO.println(v is Nil)   // false
IO.println(v != nil)   // true    <-- correct

var c: Int|Nil = nil
IO.println(c is Nil)   // false   <-- wrong
```

`is` is correct on non-union declarations, so this is specific to unions:

```saffron
var a: Int = 5
IO.println(a is Int)   // true
var b: Any = 5
IO.println(b is Int)   // true
```

**Why this is worse than a wrong answer.** The idiomatic nil check compiles
cleanly and its body never runs:

```saffron
var v: Int|Nil = m.get("a")
if (v is Int) {
    IO.println("${v}")     // never reached; program prints nothing, exits 0
}
```

There is no diagnostic. A program that looks like it handles the present case
silently handles neither.

**Three different behaviours for the same intent**, which is how this stayed
hidden — only one of the three is both accepted and correct:

| Guard | Result |
|---|---|
| `if (v != nil) { IO.println("${v}") }` | works, prints 5 |
| `if (!(v is Nil)) { IO.println(v) }` | works, prints 5 |
| `if (!(v is Nil)) { IO.println("${v}") }` | **compile error**: "cannot call .to_string() on nullable 'v'" |
| `if (v is Int) { ... }` | **silently dead** — guard is false |

So `!(v is Nil)` narrows for a direct `IO.println` but not through string
interpolation, while `v is Int` does not narrow *and* evaluates false. Use
`v != nil`, which is the only form that both narrows and evaluates correctly.

**Cause.** `gen_is_check` (`src/compiler/codegen/expr_body.sf:402`) has two
paths, and the union falls into the wrong one. It emits a *runtime* NaN-box tag
check only when the static type is the exact string `"Any"`:

```saffron
var val_type_str: String = this.type_to_string(val_type)
if (val_type_str == "Any") {
    // ... __val_is_int / __val_is_nil / etc.
```

`Int|Nil` is not `"Any"`, so control reaches the compile-time branch below
(`:439`):

```saffron
var matches: String = "0"
if (type_name == "Int" or type_name == "Number" or type_name == "Float") {
    if (this.is_int_type(val_type)) { matches = "1" }
```

`is_int_type(Int|Nil)` is false and `is_nil_type(Int|Nil)` is likewise false, so
`matches` stays `"0"` and a literal constant `false` is compiled in. That is why
both arms are dead and why there is no diagnostic — from codegen's point of view
it statically proved the test.

**Suggested fix.** A union operand is exactly the case that needs the runtime
check, so the `val_type_str == "Any"` gate should also admit unions — any type
that is not a single concrete type cannot be decided at compile time. Better
still, invert the condition: take the compile-time path only when the operand's
type is a single concrete type, and emit the runtime check otherwise. The
`"Any"`-only spelling is a whitelist that silently mis-answers everything it
forgot.

**Fixed** by taking the suggested inversion. `types_body.sf` gained
`is_undecidable_type`, which matches on the AST variant rather than comparing a
rendered type string, and answers true for `AnyType`, `UnionType` and
`NullableType`. `gen_is_check` now gates the runtime branch on that predicate.
Matching the variant is what makes it safe: any new multi-representation `Type`
variant must be added to the match arms or the build fails, so the whitelist
cannot silently fall behind the type system again.

`NullableType` was folded in on the same reasoning even though the reproduction
above uses a union — every `is_*_type` predicate answers false for it too, so it
was the same latent bug one spelling away.

**One case deliberately still answers false:** `is <ClassName>` on an
undecidable operand. Values carry only a NaN-box tag, not a class identity, so
there is nothing to test at runtime. It now emits a codegen warning instead of
answering silently — a dead `is` branch is invisible at runtime, and that
invisibility is what made this bug expensive. A correct answer needs runtime
class tags, which is a separate piece of work.

Regression test: `test/pass/is_union.sf` (15 assertions; the pre-fix compiler
fails 5 of them).

**Relationship to the old #11.** `#11` ("Flow narrowing for primitives in
unions") is listed under Fixed, and that fix was real — it addressed the checker
reading `Expr.type` instead of `Expr.self.type`. This is a separate defect in
codegen rather than the checker, hence a new entry rather than a reopen.

### 64. FIXED — a request body over ~35 KB silently killed the server; `@http/server` read only 8192 bytes and never checked for a short read

**Reproduction** — against any Saffron HTTP server (this is the playground's, but
the server code is `@http/server`, not the playground's):

```
body   8532B  ok
body  16014B  ok
body  32014B  ok
body  40014B  server gone, no further request answered
```

The process exits silently, exactly as in #63, and every subsequent request gets
`ECONNREFUSED`.

`_handle` reads the request with a single fixed-size call and never looks at how
much it got (`src/lib/http/server.sf:400-405`):

```saffron
var raw: String = ""
if (tls_conn != nil) {
    raw = tls_conn.read(8192)
} else {
    raw = conn.read(8192)
}
```

There is no loop to drain the socket and no `Content-Length` check, so for any
body larger than the buffer the parser is handed a **truncated** request: headers
possibly intact, body cut mid-way, and — because the read is a single syscall on a
non-blocking socket — sometimes cut mid-*headers*. Nothing downstream is prepared
for that. Bodies in the 8–32 KB range happen to survive because the read returns
more than 8192 in practice on loopback; past ~35 KB it does not, and the
truncated-parse path takes the process down.

Two independent defects:

1. **The read is not a loop.** A correct HTTP server reads until it has the
   headers, parses `Content-Length`, then reads exactly that many more bytes.
2. **A short read is not detected.** `raw.length() == 0` is checked
   (`server.sf:407`), but not `raw.length() < expected`, so truncation is
   indistinguishable from a complete request.

Consequence for anything trying to enforce a request-size limit: it cannot. The
playground sets `MAX_SOURCE_BYTES = 64000` and checks it in application code, but
**the check is unreachable** — the server dies at ~35 KB, well below the limit, so
the cap can never fire. A size limit has to be enforced by the server while
reading, not by the handler afterwards.

Also a trivial remote denial-of-service: one 40 KB POST, no authentication
needed, and it is a different mechanism from #63 (that one needs ~85 requests to
reach a GC cycle; this one needs a single large one).

**FIXED** (`e6b0735`). Both defects. `_read_request` now drains until the header
terminator is in hand, then reads exactly `Content-Length` further bytes,
detecting short reads explicitly and enforcing the size cap *while* reading — so a
request-size limit is now actually enforceable, which it could not be before.
Short reads and unterminated headers answer 400 instead of truncating silently,
and oversize bodies answer 413.

The root cause is one level down and remains as-is: `Net._raw_read` breaks after
its first successful `recv`, so one call yields at most one TCP segment regardless
of the size requested. The old code's single retry covered a body spanning exactly
two segments and no more, which is why ~8–32 KB worked on loopback.

A/B measured against a live server with only `server.sf` differing — before, 200 KB
gets no response at all and 2 MB is truncated to exactly 65536 bytes; after, 5 B
through 4 MB all arrive byte-exact.

### 63. FIXED — the moving minor GC invalidated receiver pointers held in SSA temps; a three-line program segfaulted, and it was not async-specific

**The mechanism this entry originally described was wrong.** It said the GC's
shadow stack roots locals by the address of a stack `alloca`, that a coroutine's
locals move to a heap frame across a suspend, and that the collector therefore
scans a stale address. That was measured and **disproved**: root slot addresses
are byte-identical before and after `Async.sleep` across three concurrent tasks,
and shadow-stack depth is unchanged (`coro_addr.sf`). LLVM allocates the coroutine
frame once at `coro.begin` and it does not move. The original suggested fixes
("re-push roots on resume", "make `llvm.coro` frames traced objects") were aimed
at the wrong layer and would not have fixed anything.

The coroutine is **incidental**. The real defect needs no async, no server, and no
coroutine — this segfaults deterministically:

```saffron
var xs: List<String> = []
for (i: Int = 0; i < 20000; i = i + 1) { xs.push("v" + i.to_string()) }
IO.println("len=${xs.length()}")
```

**Cause: codegen loads the receiver, then allocates, then uses the stale pointer.**
The minor GC is a *moving* collector — `__gc_minor_promote` (`src/runtime/gc.ll:1707`)
copies live nursery objects to old gen and `__gc_minor_collect` resets the bump
pointer (`gc.ll:1467-1468`). `__gc_minor_update_refs` (`gc.ll:1833`) forwards
shadow-stack roots, but codegen emits the receiver load *before* the argument's
allocation, so the in-flight receiver lives only in an LLVM SSA temp — which is not
a root and never gets forwarded:

```llvm
%t3  = load i64, i64* @__g_xs                    ; receiver captured here
...
%t14 = call i8* @__sf_malloc(...)                ; allocates -> can run a minor GC
%t19 = call i64 @__list_push(i64 %t3, i64 %t18)  ; %t3 is now STALE
```

`__int_to_string` (`src/runtime/runtime.sf:443-444`) calls `__gc_alloc` directly, so
the extremely common `"v" + i.to_string()` form allocates on exactly this path.

**It corrupts silently as well as crashing**, which is worse: 300 pushes yield
`len=299`, with the loss landing precisely on a minor collection. A separate repro
shows 143 of 400 values mismatched.

**Controls that pin the cause** (each re-verified independently):

| control | result |
|---|---|
| default (256KB nursery) | segfault |
| `__gc_disable()` | clean |
| 256MB nursery (never fills) | clean |
| `__gc_set_threshold(1e9)` — major GC only | **still crashes** |
| nursery bypassed in `gc.ll` | clean |
| promote but never reset the bump pointer | clean |

The fourth row is the decisive one: suppressing the *major* GC does not help, while
a nursery that never fills does. So this is the **minor** collector. The last row
shows it is a stale pointer *after a move*, not a use-after-free.

**Scope is much wider than "every HTTP server."** It hits lists, maps, closures and
plain read-back loops. The HTTP server is simply a reliable way to allocate in a
loop: same server and 10-byte payload died at request 86, while the identical
binary with a 1GB nursery served 800 requests clean.

**Fix.** Reload the receiver from its root slot *after* argument evaluation,
immediately before the call — i.e. sink the receiver load below all allocating
argument code in the arg-emission path (`src/compiler/codegen/methods_body.sf` /
`expr_body.sf`). Medium size, low risk, directly testable with the repros above.
Interim mitigation with real value: make the nursery much larger or opt-in
(`@__gc_nursery_size`, `gc.ll:55`), which empirically eliminates every repro
including the server. Note the deeper point — **values in SSA temps are not roots,
so no moving collector can be correct against this codegen**; making the young
generation non-moving (or removing the nursery) fixes this and #81 together.

The original server reproduction and the payload-size analysis follow, and remain
accurate — only the *explanation* was wrong.

**Original reproduction (still valid):**

**Reproduction:**

```saffron
import "@http/server" as HttpServer

var app = HttpServer.server(8098)
app.get("/small", fun (req: Any): Any {
    return HttpServer.Response(200, "0123456789")
})
app.serve()
```

```
DIED at req 85, cumulative body bytes=840: Remote end closed connection without response
```

Eighty-four successful 10-byte responses, then the process is gone. No panic, no
stderr, no log line — the last thing in the log is still `Listening on ...`, so
from the client side it looks like a network fault rather than a crash.

It is the collector. Adding one line makes it vanish:

```saffron
@extern("i64 __gc_disable()") fun gc_disable(): Int
gc_disable()
```

399 requests clean, versus 84, with nothing else changed. The RSS trace shows the
collection immediately before the death — heap peaks at req 78, shrinks at req 79,
process dies at 85 — so the crash is the first *use* of something the collection
freed, not the collection itself. File descriptors are flat at 10 throughout and
RSS never exceeds 6 MB, so it is neither an fd leak nor exhaustion.

Response size only sets the timing, by reaching `@__gc_threshold` (65536 bytes,
`src/runtime/gc.ll:985`) sooner:

```
20000B responses -> died on request 4    (cumulative 60000)
10B    responses -> died on request 85   (cumulative 840)
```

A later measurement against the playground put the death at **request 23**, not
85. That is not a contradiction of this entry but a confirmation of the mechanism
above: the playground's responses (JSON with a base64 wasm payload) are far larger
than 10 bytes, so the cumulative-byte threshold arrives sooner. **Quote a request
count only together with the response size** — the count alone is meaningless, and
"~85" in this entry's title is specifically the 10-byte case.

The GC is not broken in general — straight-line allocation is fine:

```saffron
var total: Int = 0
for (i: Int = 0; i < 200000; i = i + 1) {
    var s: String = "chunk-" + i.to_string()
    total = total + s.length()
}
IO.println("survived: ${total}")     // survived: 2288890
```

The distinguishing feature of the server is that every connection is handled in a
spawned task (`src/lib/http/server.sf:379-383`), so `_handle`'s heavy allocation
happens inside a **coroutine frame**. `__gc_push_root` roots a local by taking the
address of a stack `alloca`; in a coroutine those allocas live in a heap frame
that can move across a suspend, so the collector scans a stale address and never
sees the live object. 4000 spawn/await round trips that only compute do *not*
fail, which fits — the trigger needs a task that actually suspends on I/O, as
`accept`/`read`/`write` do.

Impact: a hard blocker on writing any real server in Saffron, and remotely
triggerable with ~85 unauthenticated GETs. `__gc_disable()` trades the crash for
an unbounded heap; the fix belongs in making the shadow stack coroutine-aware
(re-push roots on resume, root via the frame pointer, or make `llvm.coro` frames
traced objects).

**FIXED via option 3 — the nursery is retired.** `__gc_init` no longer calls
`__gc_nursery_init()`, so `__gc_nursery_inited` stays 0 and `__gc_alloc` takes
the old-gen path unconditionally. With no moving young generation there is no
move to invalidate an SSA temp, and this and #81 close together. The repro at the
top of this entry now prints `len=20000`; major collection still recycles
(120000 objects allocated, `bytes=86152` resident).

Everything is left intact and reachable: the nursery code, its stats externs, and
`__gc_set_nursery_size` all still work, and calling `__gc_nursery_init()` by hand
re-enables the whole path. `src/runtime/gc.ll`'s nursery section header carries
the full explanation of why it is off. Two notes for whoever revives it:

- The blocker is unchanged and is **in codegen, not the collector**: values in
  SSA temps are not roots, so no moving collector can be correct against this
  codegen. Sink the receiver load below all allocating argument code first
  (`methods_body.sf` / `expr_body.sf`), or make the young generation non-moving.
- The forwarding pass has its own tag bug on top of that — see #101 and the
  comment in `__gc_minor_visit_slot`.

`test/gc_generational_test.sf` asserts on bump allocation and minor collections,
so it fails by design while the nursery is off; `run_tests.sh` classifies it
under `NOT_A_TEST` with that reason. It is exactly the test to re-enable
alongside a non-moving young generation.

### 62. FIXED (map half) — `for (entry in someMap)` compiled to a list index loop and segfaulted; the documented iterator protocol is still never used

**Reproduction:**

```saffron
var m: Map<String, Int> = {"one": 1}
for (e in m) { IO.println(e[0]) }   // Segmentation fault: 11
```

`CLAUDE.md` documents map iteration as supported ("Iteration yields [key, value]
pairs") and that program crashes.

`Parser.desugar_for_in` (`src/compiler/parser.sf:2193-2211`) has no type
information, so it unconditionally emits an index-driven while loop with the
source typed `"Any"`:

```saffron
var init_list: AST.Stmt = AST.Stmt.VarDecl(list_var, "Any", iter_expr, "")
var cond: AST.Expr = ... MethodCall(Variable(list_var), "length", []) ...
var item_init: AST.Stmt = AST.Stmt.VarDecl(item_name, "Any",
    AST.Expr.IndexGet(AST.Expr.Variable(list_var), AST.Expr.Variable(idx_var)), "")
```

Two things then go wrong in the IR: `src.length()` on a Map dispatches to
`@__list_length`, reading a map header as a list header; and `src[i]` reaches
`gen_index_get`'s Map branch check (`src/compiler/codegen/methods_body.sf:418`),
which is gated on `typed_vars` knowing the object is a Map — it is `Any` here, so
the branch is skipped and the integer cursor is passed to `__map_get` as a **key**.
The crash is the `e[0]` list-read of the resulting garbage.

The same mechanism means the protocol `CLAUDE.md` promises ("uses iterator
protocol: `.iter()` -> object with `.has_next()`, `.next()` ... works over Lists,
Strings, Maps, and custom types") is never used at all:

```saffron
class Countdown {
    fun iter(): Countdown { return this }
    fun has_next(): Bool { ... }
    fun next(): Int { ... }
}
for (x in Countdown(3)) { ... }   // [codegen] Error: type 'Countdown' has no method 'length'
```

Lists and Strings work only because they happen to be indexable. Desugaring to
the promised protocol — `var it = coll.iter(); while (it.has_next()) { var item =
it.next(); ... }` — is the right shape, since it is type-agnostic (what the parser
needs) and removes the `length()`/`[i]` special-casing from codegen. But it
cannot be done as written today; see the addendum.

Pre-existing, not a regression: reproduces identically on the pre-session
baseline compiler.

**Addendum (2026-07-30), re-verified while correcting the docs.** Three
corrections to the above.

**`Map.iter()` does not exist.** This entry claimed it "already exists and is
correct when driven by hand". It does not exist anywhere — `grep -rn '"iter"'
src/compiler/ src/runtime/runtime.sf` returns nothing. So the suggested fix has a
prerequisite: builtin collections need real `iter()` methods before `for-in` can
be desugared to call one.

**The manual protocol form is a *silent* no-op**, which makes it worse than the
`for-in` case rather than a working alternative to it:

```saffron
var xs: List<Int> = [1, 2, 3]
var it = xs.iter()
while (it.has_next()) { IO.println(it.next()) }
// compiles; exit 0; NO output. Only hint is
// "[checker] Warning: it: cannot infer type, add explicit annotation"
```

`for (x in xs)` over the same list correctly prints 1/2/3, so this is specific to
the `.iter()` path. The call dispatches dynamically on an `Any`-typed receiver,
resolves to nothing, and yields something whose `has_next()` is immediately
false. A `MethodCall` on an `Any` receiver that resolves to no known method must
become a diagnostic — that is the general dynamic-dispatch hole and belongs with
the resolve pass (`docs/design/compiler-rewrite.md`, stage 2), not a spot fix
here.

**The Map crash is not an inference problem.** All four spellings segfault
identically — map literal directly in the loop header, a `var` with an explicit
`Map<String, Int>` annotation, and an inferred `var`. The annotated form crashes
too, because the desugaring never consults the receiver's type at all. A narrow
stopgap that would at least stop the crash: have the desugaring recognise a
statically-known Map receiver and emit the `keys()` form.

**Resolution (2026-07-31): the Map half is fixed; the iterator protocol is
still absent.** `for (entry in m)` now walks the entries and yields a
`[key, value]` pair, which is what `CLAUDE.md` always documented. The repro at
the top of this entry prints `one` / `two`.

The diagnosis above was wrong on one point worth recording, because it is the
kind of error that sends a fix to the wrong layer. It claimed the Map branch of
`gen_index_get` was "gated on `typed_vars` knowing the object is a Map — it is
`Any` here, so the branch is skipped". It is not skipped: `"Any"` is the parser's
*inference sentinel*, not a claim that the type is unknown, so `__for_src` gets
the receiver's real type and the emitted IR contained `__map_get` all along. The
element read was reaching the Map path and doing the wrong thing there, rather
than falling through to the list path.

The actual defect is that "element at position i of an iteration" and "value for
key k" are different operations that the desugaring spelled the same way. On a
`Map<String,_>` the integer cursor missed every key (nil, and originally a
segfault via the `e[0]` read of the result); on a `Map<Int,_>` it would have
looked up the keys 0, 1, 2… instead of walking the entries — silently plausible,
and the more dangerous of the two.

The fix separates the two operations:

- **`parser.sf` `desugar_for_in`** emits `src.__iter_get(i)` instead of `src[i]`.
  Spelled as a `MethodCall` on a reserved name rather than a new `AST.Expr`
  variant deliberately: a variant needs arms at ~30 match sites and a missing arm
  yields an indeterminate value rather than an error (#76), so it is the riskier
  encoding of the same thing.
- **`runtime.sf` `__any_iter_get`** discriminates on the receiver's GC type tag
  via the existing `__rt_as_map_ptr` / `__rt_as_list_ptr`, returning a
  `[key, value]` list for a Map, `__list_get` for a List, and a bounds-checked
  `__str_get` otherwise. Runtime dispatch rather than static, for the same reason
  `__any_length` exists: it is what makes `for-in` work over an `Any`-typed or
  generic receiver, which is the case that made this reachable through
  `iter.sf`'s `map`/`filter`/`reduce`. The pair is returned **untagged**, like
  every other container — `__gc_is_heap_ptr` rejects NaN-tagged values, so
  tagging it would get it swept while live (#23).
- **`methods_body.sf`** handles `__iter_get` above every arm that emits IR, since
  the `Alias.method()` dispatch below it runs an unconditional `gen_arg_value` on
  the receiver (the preamble hazard that made `super` fail verification, #70).
- **`checker.sf` / `types_body.sf`** type the element: `T` for a `List<T>`,
  `String` for a String, and `List<Any>` for a Map. Not `List<K|V>` — that would
  type check and then silently take the wrong branch, since `is` is broken on
  union types (#69).

`__any_iter_get` also had to go into `known_functions` at all three sites in
`codegen.sf`. Without it the auto-declare loop in `output_body.sf` emitted
`declare i64 @__any_iter_get(i64)` at its default arity of 1, redefining the
two-argument declare from `runtime_declares()`; STAGE 2 caught it as an invalid
redefinition. Exactly the shape of #85, and worth noting that stage 1 passed —
only the gen4 fixed-point check failed.

`m.length()` was never actually broken, despite the entry implying it: a Map
dispatches to `__list_length`, which happens to be correct because `count` is the
first field of both headers. Left alone rather than "fixed", since changing it
would be churn with no behaviour change.

**Still open, and the reason this entry is only half-fixed:** there is no
iterator protocol. `for (x in Countdown(3))` over a custom type remains
`[codegen] Error: type 'Countdown' has no method 'length'` — a clear compile
error rather than a crash, but the protocol six documents once promised does not
exist. Adding it means real `iter()` methods on the builtins plus a desugaring to
`has_next()`/`next()`, and the silent-no-op hole below (a `MethodCall` on an
`Any` receiver resolving to nothing) has to be closed first or the protocol form
will keep failing quietly. That belongs with the resolve pass
(`docs/design/compiler-rewrite.md`, stage 2).

Six documents asserted the protocol works — `CLAUDE.md`,
`docs/src/tutorial/iterators.md` (an entire page), `docs/src/introduction.md`,
`docs/src/tutorial/lists-and-maps.md`, `docs/src/tutorial/control-flow.md`, and
`docs/src/stdlib/iter.md`. All six are now corrected to describe `for-in` over
builtins, which does work, plus the `keys()`/`values()` workaround for Maps.
`src/lib/iter.sf`'s module doc already said the protocol was missing and was
right all along.

### 61. FIXED — `is` on a class name is now a real runtime check, including subclasses and `is`-pattern `match`

**Reproduction:**

```saffron
class Dog { fun init() {} }
class Cat { fun init() {} }
fun check(a: Any): Bool { return a is Dog }
IO.println(check(Dog()))   // false — wrong
IO.println(check(Cat()))   // false — correct by accident
```

`Codegen.gen_is_check` (`src/compiler/codegen/expr_body.sf:445`) maps a fixed list
of *builtin* names to runtime predicates. For anything else — a class or enum
name — it gives up and emits a literal false. That path now at least warns
(`expr_body.sf:486`), which is how the two shapes below were caught.

**This entry originally claimed the machinery already existed and the fix was to
"fall through to the same `__gc_get_type_tag` comparison" used by the statically
typed path, and it showed IR to prove it. That IR does not exist and the claim was
wrong.** The static path only ever emits `add i64 0, <0-or-1>`
(`expr_body.sf:509-510`). The `true` that made `d is Dog` look like it worked is a
**compile-time constant from `is_class_type`**, not a runtime query. The only
`__gc_get_type_tag` calls anywhere in a compiled module are inside the three
`__reflect_*` helpers (`stmts_body.sf:1435`, `:1476`, `:1509`) — nothing `is`
emits reaches it. Verified in the IR: `d is Dog` is `%t8 = add i64 0, 1`.

So the real defect is broader than "specific to a genuinely opaque value": **class
identity is never tested at runtime at all**, and every spelling where the static
type is not exactly the class folds to a constant.

Because the fold is also **flow-insensitive**, a local the checker cannot infer
answers wrong too — a case this entry used to miss entirely:

```saffron
fun pick(b: Bool): Any { if (b) { return Dog() } return Cat() }
var d: Any = Dog()
IO.println(d is Dog)        // true  — but only because the initialiser refines it
var z: Any = pick(true)
IO.println(z is Dog)        // false — WRONG, z holds a Dog
```

`z` gets `[checker] Warning: z: cannot infer type`, so `is_class_type` answers
false and the check folds away. Same silent-wrong-answer class as the parameter
case.

Knock-on: `is`-pattern matching inherits it, so the `match (animal) { is Dog(d)
=> ..., is Cat(c) => ... }` form `CLAUDE.md` documents is broken. **Correcting a
second claim in this entry:** it said every arm tests false so "the first arm wins
— `sound(Cat())` returns `"Woof"`". It does not. *Both* calls fall through to the
`_` arm and return `"?"`; with no `_` arm the match has no viable arm at all.
Measured:

```
d is Dog: true      sound(Dog): ?
z is Dog: false     sound(Cat): ?
```

Wrong answer either way, but not by the mechanism described. On the baseline
compiler this program failed to compile (`undefined variable 'd'`), so the path
has moved from "rejected" to "silently wrong".

**Fix.** Emit a real runtime check in *both* branches — the undecidable one at
`expr_body.sf:486-489` and the static class fold at `:508` — comparing
`@__gc_get_type_tag(val)` against the `class_type_ids` entry for `type_name`. The
tags are already assigned per class in `gen_class_constructor`
(`stmts_body.sf:613-616`) and already baked into each `__gc_alloc` call
(`:629-633`), so **no runtime work is needed** — that part of the original entry
was right. Removing the static fold is what fixes the `z` case, and it is the
reason this is not just a one-line patch to the `Any` branch.

Note that a correct `is` for a *subclass* additionally needs the parent chain,
which codegen does not have (see #50) — `class_parents` lives only in
`checker.sf:163`. Exact-class identity is fixable now; `d is Animal` for a `Dog`
is not.

**Resolution (2026-07-31).** Both halves are fixed, plus the subclass gap the
last paragraph called out of scope. Codegen now records the hierarchy it was
already handed: `gen_class_decl_with_parent` writes `class_parent_of`
(`codegen.sf`, keyed by struct name so a base class in another module still
joins), and the `generate()` pre-registration pass writes it too so a forward
`x is Dog` resolves (`output_body.sf`, the BUGS #33/#36/#37 ordering hazard).
Two emitted helpers back it: `__class_parent_tag` (a switch, one arm per class
with a parent) and `__class_is_a` (walks the parent chain, zero-test before
equality so a target tag of 0 answers false) — see `emit_class_hierarchy_helpers`
in `stmts_body.sf`. `__val_class_tag` (`base_nanbox.ll`, `wasm_base_32.ll`; a
`ret 0` stub on wasm64 which has no GC header) reads the tag with
`__gc_is_heap_ptr`-style guards, and crucially accepts *both* the TAG_PTR and the
bare-untagged-pointer representations, because a fresh constructor result is
untagged. `gen_is_check` now calls `gen_class_tag_check` in both the undecidable
branch and the static-fold `else`; `gen_match`'s class-pattern branch emits an
`__class_is_a` if/else chain instead of picking one arm at compile time
(`match_body.sf`). Both new helpers return `""` / false for a non-class name so
the "I don't know" channel is preserved (a dead `is` branch stays visible).

Verified: `d is Dog` true, `d is Animal` true (needs the parent chain), the
`z: Any = pick(...)` case true, and `match (a) { is Dog(d) => ..., is Cat(c) =>
..., _ => ... }` returns `Woof / Meow / ???`. Bootstrap passes to the gen4 fixed
point; test suite has zero regressions and `comprehensive` newly passes.

**What this does *not* fix:** wasm64 (`wasm_base.ll`) still has no per-class tag
(its `__gc_alloc` discards the tag), so `x is SomeClass` there is still
unanswerable — `__val_class_tag` returns 0 honestly rather than reading garbage.
A generic parameter `T` and interface conformance are still not runtime-testable.
Virtual dispatch (#50) is a separate step on the same foundation.

### 60. FIXED — `for (var i = 1; ...)` parses, and a C-style loop variable is inferred

The C-style `for` header used to hard-`consume(":")` after the identifier, so the
one binding form that could not infer was the first loop anyone writes:

```
[line 1, col 13] Error: expected ':' but found '='
```

`parse_for` now takes an optional leading `var` and an optional `": Type"`,
defaulting to `"Any"` — the same inference sentinel `parse_var_decl_with_doc`
uses, which the checker's `VarDecl` arm reads as "infer from the initializer".

Verified 2026-08-01, all three spellings run and print correctly:

```saffron
for (var i = 1; i <= 3; i = i + 1) { IO.println(i) }              // 1 2 3
for (j: Int = 0; j < 2; j = j + 1) { ... }                        // j0 j1
for (var k: Int = 7; k < 9; k = k + 1) { ... }                    // k7 k8
```

### 59. FIXED — a function's local variable wrote into a like-named module global

Fixed alongside the resolve pass. `gen_function` now records the body's own
`var`/`let` names (from `collect_vars`) in a `current_fn_locals` set, and the
three declaration sites — the alloca in `gen_function`, the GC-root push, and the
store in `gen_var_decl_with_name` — prefer that set over `module_globals`. A name
the current body declares is a local, so it gets its own slot and its store
targets that slot. The set is empty at top level / module init, where a `var`
really is a global; it is saved/restored around nested function bodies. Both the
example below and the nested-closure variant now print `5`.

**Reproduction (now correct):**

```saffron
var i: Int = 5
fun f() { var i: Int = 99 }
f()
IO.println(i)          // was 99 (local overwrote the global); now 5
```

No loop required, no aliasing on the call, no shared name passed anywhere. A
`var` declaration inside a function body silently becomes a *store into the
global* whenever the module has a global of the same name. The emitted IR has no
local slot at all:

```llvm
@__g_i = global i64 0
define i64 @f() {
entry:
  %t1 = add i64 0, 99
  %t2 = call i64 @__val_tag_int(i64 %t1)
  store i64 %t2, i64* @__g_i      ; ← should be a local alloca
  ret i64 0
}
```

`for-in` loop variables do it too (`fun f() { for (item in [1,2,3]) {} }` leaves
a global `item` at 3), which is how this was found: a `while (i < 3)` loop whose
body called `Random.shuffle` — whose own loop counter is named `i` — never
terminated, because each call reset the caller's counter to 0.

**Cause.** `gen_var_decl` in `stmts_body.sf:216-223` picks the store destination
by asking `module_globals.has(name)` and nothing else. There is no check for
whether the declaration is inside a function body, so scope is never consulted.
The matching alloca is not emitted either: `emit_block_alloca_for`
(`stmts_body.sf:1007`) skips any name already in `typed_vars`, and the global put
it there — so naively redirecting the store to `%i` would emit a reference to an
undefined value rather than fix anything. Both halves have to move together.

**Relationship to #40.** Same root cause — global-before-local name resolution —
but the opposite direction and a distinct failure. #40 is a *read*: a callee's
parameter resolves to a like-named global, so `Math.sqrt(16)` returns
`sqrt(42)`. This one is a *write*: a callee's local clobbers the global, so
unrelated caller state changes underneath it. #40's reverted fix attempt
(`is_current_param`) would not have touched this case, since a `var` declaration
is not a parameter. The `current_params`-style scope tracking that #40 calls for
is the shared prerequisite: this is I2/M1 territory in
`docs/design/compiler-rewrite.md` — resolution decided per-name at emission time
instead of by a real scope structure. A resolve pass with `DefId`s (stage 2 of
that document) removes the whole family at once.

**Severity.** Higher than #40's. It is trivially reachable — any stdlib helper
with a loop counter named `i`, `n`, or `idx` corrupts a user program that happens
to have a global of that name — it fails as a hang or as silent state
corruption rather than a wrong-looking number, and the affected code is
correct-by-inspection in both files.

### 50. FIXED — an overridden method is now dispatched on the receiver's runtime class

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

**Re-verified 2026-07-30, and it is structural for two independent reasons.** The
forwarder is emitted at `src/compiler/codegen/stmts_body.sf:368-399` and is
exactly a tail-call: `Dog__intro` is `%r = call i64 @Animal__intro`, and
`Animal__intro` calls `@Animal__speak` by static name. Two-level chains and an
`Animal`-typed variable holding a `Dog` all print the base version.

What blocks the cheap fix:

1. **Codegen has no parent map at all.** `class_parents` exists only in
   `checker.sf:163`; there is no equivalent on `Codegen`, and `grep -rn
   'vtable\|virtual' src/compiler/` returns nothing. `class_methods`
   (`codegen.sf:32`) stores method *name strings*, not ASTs, so the parent method
   bodies needed for per-subclass re-emission have already been discarded by the
   time a subclass is lowered.
2. **Emission order.** `Animal__intro` is emitted before `Dog__speak` (IR lines
   202 and 234 in the repro), so when the parent body is lowered the compiler does
   not yet know which subclasses override what.

So neither of the two routes is a local change, which is why this stays filed as a
design limitation rather than being handed to a fix pass.

**`CLAUDE.md` currently advertises polymorphic `speak()` overriding as supported,
so the documentation oversells this.**

**Resolution (2026-07-31).** Both blockers named above were removed by the #61
foundation (a real parent map in codegen, and per-class tags assigned in the
`generate()` pre-registration pass — so emission order no longer matters). The
fix is the tag-switch route, not vtables. At a method call that binds
statically to `<ns>__<method>`, `gen_virtual_dispatch` (`methods_body.sf`)
switches on `__val_class_tag(receiver)` with one arm per descendant of `ns`
whose *effective* implementation of the method differs from `ns`'s, calling that
subclass's own symbol; the default arm is the static symbol. Correct by
construction: every class in the subtree already has a `C__method` symbol (a
real override, or the forwarder `gen_class_decl_with_parent` emits), so an arm
per overriding descendant covers every receiver. It returns `""` when nothing
overrides (the common case pays nothing) and in identity mode (the bootstrap
keeps static behaviour). Two new pre-scan tables back it: `class_own_methods`
(methods a class declares with a body, before forwarder mixing) and
`class_bare_of` (struct name → bare name), both keyed by struct name like
`class_parent_of`. The hook sits after the coroutine/actor dispatch guards and
operates on the already-evaluated receiver, so it does not re-enter the receiver
expression — the #70 preamble hazard is avoided.

Verified: `d.describe()` → `Rex says Woof`; a `Cat` in an `Animal`-typed slot
dispatches `Meow` (the `List<Animal>` case the entry said needed a separate
step); a two-level `Puppy extends Dog extends Animal` chain gives `Max: Yip`;
`describe()` (not itself overridden) picks up the override through its inner
`this.speak()`, which is itself dispatched. Every method call site changed;
zero test regressions.

Uncovered while testing, filed as #49-adjacent: a grandchild that declares no
`init` (`Puppy` with only `Dog` and `Animal` defining `init`) compiles
`Puppy("Max")` to a bare `@Puppy()` with the argument dropped, so its fields
stay 0 and the first field read segfaults. The `init` forwarder
(`stmts_body.sf:384`) gates on `called_function_arity` for the *immediate*
parent, which a forwarded `Dog__init` never registers, so the chain breaks at
the second level. Independent of dispatch — reproduces by giving each level an
explicit `init`, which works.

### 51. FIXED — mutation of a captured variable was lost (captures were by value)

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

**Re-verified 2026-07-30, and the entry as filed was conflating two independent
defects.** The repro above still fails exactly as written, but only because it
uses `Float`. Separating the factors:

| shape | result |
|---|---|
| `Int` capture, closure at **module top level** | `1`, `2`, `count = 2` — **fully correct** |
| `Int` capture, closure **inside a `fun`** | `1`, `2`, `count = 0` — closure sees its own writes; enclosing scope does not |
| `Int` capture, closure returned from a factory | `1`, `2` — **correct**, independent instances |
| `Float` capture, either scope | `0` every time — nothing works |

So there is no single "captures are by value" bug:

1. **A `Float`-typed capture is broken outright**, in any scope, and this is the
   more severe half. It is not a closure bug at all — plain `Float` arithmetic
   without any closure (`var c: Float = 5.0; c + 1`) correctly gives `6`, so the
   breakage is specific to a `Float` crossing the capture boundary. This is very
   likely the same float-tag/untag confusion as #52 and #54 rather than the
   by-value hoisting described below, and should be investigated with them.
2. **The by-value hoisting is real but narrower than filed**: with `Int` it only
   loses the write-back to an enclosing *function's* frame. The closure's own
   view of its counter is correct, and top-level captures are fully correct — so
   the `output_body.sf` analysis below explains the third row only.

The practical consequence for the docs, at the time of that re-verification, was
that `CLAUDE.md`'s mutable-counter pattern did not work as shown if the counter
was a `Float` or the closure was nested in a function. Both halves are fixed now
— see the resolution below.

`src/compiler/codegen/output_body.sf:26-85` hoists nested functions to top level
with their free variables appended as ordinary by-value `i64` parameters
(`find_free_vars_stmts` → `full_params`), so a write assigns to the callee's own
copy. Real mutable capture needs boxed cells — captures passed as pointers, with
the enclosing frame's variable promoted to a heap cell — which the comment at
line 26 hints at ("capture POINTERS") but the code does not do.

**`CLAUDE.md` showed a mutable-counter closure as a supported pattern, so the
documentation was wrong here too** — moot as of the fix below: the pattern now
works as documented, in every scope and for both `Int` and `Float`.

**Partial resolution (2026-07-31): defect A (the Float half) is fixed; defect B
remains.** The `Float`-capture breakage was the same float tag/untag confusion as
#52/#54, and the accumulated float fixes (the annotated-decl widening at
`stmts_body.sf` gen_var_decl_with_name, plus #52's index conversion) removed it.
Measured now, a `Float` capture behaves identically to an `Int` one: the closure
sees its own writes (`1`, `2`) and the enclosing frame does not (`count = 0`) —
i.e. only defect B is left, in both types.

Defect B (write-back to an enclosing *function's* frame) was the real heap-boxing
work. Diagnosis mapped it precisely: the env stored a `load i64` snapshot of each
capture (`closures_body.sf` gen_lambda, the capture-store loop ~103-111), and
inside the closure `%<cap>` aliased the env slot via GEP (gen_closure_function
~316-329) — so mutations persisted across calls of one closure instance but never
reached the enclosing frame, which kept its own alloca.

**FIXED 2026-07-31.** The repro at the top of this entry now prints `1`, `2`,
`count = 2` for both `Int` and `Float`, so #51 is closed.

The fix turned out to need far less than the diagnosis above predicted. It does
*not* route the enclosing frame's reads and writes through anything new, and so it
never touches `expr_body.sf` or `stmts_body.sf` — the hottest path in codegen is
untouched. The reason is that `%<name>` is already an `i64*` in both worlds: an
`alloca i64` and a `bitcast` of an 8-byte `__sf_malloc` cell are the same type, so
every existing load and store site downstream works unchanged and only *how the
pointer is produced* differs. What actually changed:

- **`closures_body.sf`** — a three-function analysis walker
  (`collect_lambda_assigned_stmts` / `_stmt` / `_expr`) that collects names
  assigned *inside* a nested lambda. The expression walker is an exhaustive match
  over all 30 `AST.Expr` variants, modelled on `find_free_vars_expr`; the `Lambda`
  arm recurses with `in_lambda = true` and the `Assign` arm only records a name
  when that flag is set. It deliberately skips nested `FunDecl`/`ClassDecl`/
  `EnumDecl`, which have their own frames.
- **`output_body.sf`** — `gen_function` computes the boxed set (captured *and*
  assigned, per the containment below) and emits `__sf_malloc(8)` +
  `bitcast` + `store i64 0` in place of the `alloca` for those names. The zeroing
  is unconditional, unlike the existing GC-root-type-only zeroing, because
  malloc'd memory is uninitialized where an alloca'd slot got its 0 from the
  store below.
- **`gen_lambda`** — the env-store loop writes the cell *pointer* for a boxed
  capture instead of a `load` snapshot, and `gen_closure_function` dereferences
  one extra level to make `%<cap>` the cell rather than the env slot.

Containment, as planned: box *only* locals that are both captured by a nested
lambda and assigned. A capture that is merely read keeps its alloca (the snapshot
is already correct for it) and so does every local no lambda mentions, which is
nearly all of them.

Three things worth knowing about the result:

- **Two closures over the same boxed local now share it.** That is the point, not
  a side effect, but it is an observable behaviour change from the snapshot
  semantics — covered explicitly in the regression test.
- **A boxed local leaks 8 bytes per call** to its enclosing function, since
  `__sf_malloc` is plain `malloc` and nothing frees the cell. Closures already
  leak their env array and closure pair identically (gen_lambda), so this adds
  another instance of an existing leak, not a new class of one. The upside is
  that the cell is never swept, which is what makes pushing a GC root for its
  address safe.
- **Not fixed: a closure outliving its enclosing frame.** The cell survives, but
  the frame pops its GC roots on return, so a heap value reachable *only* through
  the cell can be swept. This is exactly the pre-existing hole for the env
  array's own snapshots — boxing neither creates nor widens it.

The analysis is gated on `!identity_mode`, per the identity-mode blind spot: the
compiler self-hosts in identity mode where there is no boxing, and grep confirms
the compiler's own source contains zero real capturing lambdas (all 5 `fun (`
matches are inside comments). So the bootstrap can neither exercise nor break
this, and the coverage is `test/pass/closure_capture_writeback.sf` — 14
assertions over the Int and Float write-back, factory independence, read-only
captures, two closures sharing one cell, a write nested inside a loop and an
`if`, and the top-level case that already worked.

Note there are **two** function-emission paths in `output_body.sf` and both
needed handling: the main one, and a separate nested-`FunDecl` path (~144-180)
that hoists to top level. Clearing `boxed_locals` in the nested path is
load-bearing even though it rarely boxes anything, because the map otherwise
still holds the *enclosing* function's set while the nested body is emitted — a
lambda in there capturing a coincidentally-named variable would be handed a
snapshot as though it were a cell pointer.

### 52. FIXED — indexing a list with a `Float`-typed value silently read element 0

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

Same underlying confusion as the identity-mode float bug (see the
`--identity-mode` comment at `tools/saffron:354-367`), but on the native target
and reachable from ordinary user code, which makes it more dangerous. #53 is a
live instance of it in the shipped stdlib.

**Severity is higher than it first looks**, and a second instance was found later
in `turmeric/tools/build.sf:74-93`, whose `while (li < lines.length())` loop used
`var li: Float = 0`. The effect was that the bundler rewrote every app's
`index.html` into N copies of its first line. It presented as
"`StringBuilder.append` repeats its first argument", and only narrowing the
StringBuilder away showed the index was at fault — the failure mode (every element
reads as the first) looks exactly like a loop-variable capture bug, so it
misdirects debugging.

Note a *literal* `list[1]` is correct and inferred (`var i = 0`) indexes
correctly, so this only shows up in the shape you get from hand-writing a counted
loop with the older `Float` spelling — which is why the `Number` retirement
(#49) keeps turning it up.

Pre-existing, not a regression: reproduced on a clean baseline worktree at commit
`11c9638` with its own committed `build/saffronc`.

**Resolution (2026-07-31).** The three index paths (`gen_index_get`'s list and
string cases, `gen_index_set`) untagged the index with `emit_untag_int`, which
reinterprets a Float-tagged value's bits. New helper `emit_untag_index(val,
idx_type)` (`types_body.sf`) converts a statically-`Float` index via
`__val_untag_float` + `fptosi` and keeps the plain int untag otherwise. Verified:
`chars[f]` → `c`, `s[sf]` → `e`, `chars[f] = "Z"` then `chars[2]` → `Z`. Zero
regressions. Note this is the same float tag/untag confusion as the Float half of
#51 and #54; the fix here is confined to index positions.

### 54. FIXED — an `Int` literal in a `Float` position yielded `nan`

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

**FIXED** in codegen at the store, by `f758f42` (which filed it as #28), reusing
`__val_untag_float` — whose int-tagged path is already a `sitofp` — rather than
adding a second conversion. The parenthetical guess above was right: `Float` params
and fields were broken too, and both are fixed.

A checker-side AST rewrite (`0.0 + expr`) was also written for this and **dropped
as redundant** — all 24 assertions of its test already passed on unmodified HEAD,
and it added 316 lines to `checker.sf` including a widening-only walk over class
method bodies. Keeping the conversion in one place is the reason not to add a
second mechanism. Its tests were kept (`a8d9366`) for coverage breadth:
`test/pass/int_to_float_widening.sf` pins a `Float` return, a `Float` param
including constructor and method params, a `Float` field both from outside and via
`this.`, a `List<Float>` element, each nested inside if/while, and the negative
direction. `test/fail/float_to_int_narrowing.sf` guards the other half of the rule
— the hazard with any coercion is that it silently becomes bidirectional.

### 55. FIXED — an `@extern` used before its declaration links against the wrong symbol

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

**Resolution (2026-07-31).** `extern_sigs` was populated lazily in source order
(`stmts_body.sf:64-69`), so a call compiled before the extern decl was reached
found the map empty, fell through to the generic call branch, and emitted `call
i64 @later`. Both pre-registration passes now register externs up front:
`prescan_fun_decl` (`utils_body.sf`) no longer opts externs out — it writes
`extern_sigs` + `func_ret_types` and returns — and the `generate()`
pre-registration pass's `FunDecl` arm (`output_body.sf`) does the same, because
`Codegen.generate` runs that pass rather than `prescan_fun_decl` and the
single-file repro takes exactly that path. Both writes are idempotent with the
lowering-time ones. Verified: the repro emits `%t3 = call double @sqrt(double
%t2)` and runs (`sqrt(1.0)` → `1`); no `@later` reference remains. Zero test
regressions.

Still open, filed separately by the diagnosis: `gen_arg_value`'s
function-reference branch emits a trampoline calling `@later` for an extern in
*value* position (not call position), which is broken independently of
declaration order.

### 56. FIXED (as a diagnostic) — a field access on the result of an indirect call read 0

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

**FIXED as a diagnostic** (`ebc0b9f`), not as a working field read — the language
change is still what would make `f(1).v` compile. Two parts:

`gen_indirect_call` was leaving `last_type` holding whatever the last
sub-expression happened to set, usually the type of the final *argument* or of the
callee. Callers read that stale value as if it described the result, so
`get_field_index` looked up the field on a class inferred from leftover state and
answered 0 for a name it could not find. **The same staleness made a `Float`
returned through a closure read as an `Int` and print 0** — the float half of #51
— because `to_string` keys off `last_type`. It now sets `AnyType`, which routes
through the runtime dispatch helpers that inspect the actual tag, so both of those
become correct rather than merely diagnosed.

The `MemberAccess` fallback then reports instead of answering with the constant 0.
The diagnostic is narrowed to a receiver that really is a call: **this same
fallback is the normal working route for a module constant like `Math.PI` and for
reflective field reads**, which either return 0 here legitimately or get resolved
later, so erroring unconditionally broke `pass/math` and `test_reflect` — one
silent wrong answer traded for a batch of false rejections.

Tests: `test/fail/indirect_call_field.sf` (the direct access, must be rejected),
`test/pass/indirect_call_field.sf` (the annotated form, plus a direct call whose
declared return type needs no annotation).

### 57. FIXED — a repeated `--lib-path` duplicated every global in the output IR

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

**FIXED.** `--lib-path` arguments are canonicalised once at startup before
`_lib_paths` is populated, rather than compared as raw strings at each use: a
relative path is made absolute against `OS.cwd()` (dropping a leading `./`),
trailing slashes are stripped, and `contains` then dedupes what is left. The
repro's two spellings collapse to one entry and the redefinition is gone.

This is path *normalisation*, not full canonicalisation — it does not resolve
symlinks or interior `..` segments, so `a/../b` and `b` are still two paths. That
is deliberate: `OS` has no realpath binding, and the failure this entry describes
comes from the relative-vs-absolute spelling that the driver itself produces.

### 41. FIXED — a nested map literal overwrote its parent (silent wrong answer)

Fixed by giving each desugared map literal a unique temporary. The parser has a
`next_map_uid()` counter (mirroring the existing `next_for_uid()` for `for-in`);
each of the two desugaring sites now names its temporary `__map` + uid, bound
*before* the recursive `parse_expr` so the outer literal keeps the lower id and
each nested literal gets its own. Distinct names also give each literal its own
alloca and GC root, which fixes the use-after-free that made the multi-entry
nested case segfault. `types_body.sf`'s `starts_with("__map")` gate still matches.

**Reproduction (now correct):**

```saffron
var nest = {"outer": {"inner": 5}}
IO.println(nest.to_string())   // was {inner: 5}; now {outer: {inner: 5}}
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

### 40. FIXED — a module global shadowed a like-named parameter (silent wrong answer)

Fixed by the resolve pass (stage 2 of `docs/design/compiler-rewrite.md`).
`Math.sqrt(16)` now returns `4`.

**Reproduction (now correct):**

```saffron
import "math" as Math
var x = 42
IO.println(Math.sqrt(16))   // printed 6.48074 (= sqrt(42)); now prints 4
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

**The fix that landed.** Not `current_params` — a real resolve pass
(`src/compiler/resolve.sf`), run before the checker over the main program *and*
every imported module in one shared `Resolver`. It maintains a scope chain and
rewrites each `Variable(name)` into `Ref(kind, name, slot)` once, where `kind` is
`local`/`param`/`global`/`func`/`self`/`type`/`unknown`. The three resolution
sites now read `kind` and never consult `module_globals`, so there is no ordering
to get wrong. Resolving modules in the *same* resolver is load-bearing: this
repro's shadowing crosses the module boundary (a main-program global `x` over
`math.sf`'s `sqrt(x)` parameter), so a per-file resolver would still miss it.

**Two latent bugs it exposed.** Both were pre-existing and had been masked
because the wrong read and the wrong write cancelled out. `module_globals` was
consulted with a bare-name *fallback* after the prefixed key, in
`stmts_body.sf`'s `gen_var_decl_with_name` and in `expr_body.sf`'s `Assign` arm.
That fallback can never fire usefully — when `current_prefix` is `""` the two
keys are the same string — so it only ever matched a global belonging to a
*different* module:

- `var p: Project = Project()` inside `@pantry_config`'s `project()` stored to
  the main program's `@__g_p` while `output_body.sf` still allocated `%p`.
  Segfault as soon as the reads were corrected.
- `i = i - 1` inside `@random`'s `shuffle()` stored to the main program's
  `@__g_i` while every read used `%i`, so the loop counter never decremented and
  `shuffle()` spun forever.

Both fallbacks are removed; only the prefixed key is consulted.

### 12. OBSOLETE — type checker segfaults on Any-typed closures in imported modules (repro targets the dead C VM)

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

**NO LONGER REPRODUCES** (checked 2026-08-02). The original repro cannot be run at
all: `src/lib/test.sf` has no `mock` function any more, so `T.mock("x")` is a
different error entirely. Rebuilding the equivalent — a module function with a
`nil`-initialised local captured by a closure returning `Any`, imported and called
from another file — compiles and runs clean both inline and across a module
boundary.

The stated root cause also describes a component that no longer exists: "triggers
`runtimeError()` which corrupts VM state during import" is the **C bytecode VM**,
which is dead and in `legacy/`. This entry predates the self-hosted compiler and
is retained only as history; if a checker segfault on nil-captured closures ever
returns, it wants a fresh entry measured against `build/saffronc`.



### 21. FIXED — `type` was a reserved keyword, unusable as a parameter/variable name

`type` was tokenized as `TOKEN_TYPE` for type alias declarations, so code using it
as a parameter name (e.g. `fun define(name: String, type: String)`) was a parse
error. The suggested fix was to make it contextual, reserved only at statement
start.

**Resolution.** That is what the lexer now does — there is no `type` keyword token
at all. `parser.sf:1290` recognises it with `is_ident_named("type")` at statement
start only, so it is an ordinary identifier everywhere else. Verified against
`build/saffronc` on 2026-07-31: `type` works as a parameter, a local, a class
field (declaration, `this.type` write and `.type` read), a `for-in` loop
variable, and a map key, while `type Name = String` still declares an alias.


### 22. Cross-module global variable access emits undefined LLVM local — FIXED

**Original report:**
```saffron
// cache.sf
var store: CacheStore = CacheStore()

// query.sf
import "./cache.sf" as Cache
Cache.store.get(key)  // <-- generated %Cache instead of @__g_basil_src_cache_store
```

Codegen emitted `load i64, i64* %Cache` — `%Cache` was undefined. The prescribed
fix was a `module_globals.has(mp_resolved)` check in the `expr_body.sf`
MemberAccess handler, and that check had already landed by the time this was
re-measured. Regression coverage: `test/cross_module_global.sf`.

**Three distinct defects were behind this entry.** The first was fixed earlier;
the other two were found and fixed on 2026-07-30.

**(a) The undefined `%Module` local.** Fixed by the `module_globals` check at
`expr_body.sf:244`. Reads, writes and read-modify-write of a cross-module global
all resolve to `@__g_<prefix><name>` and produce correct results.

**(b) A method call on a cross-module global was silently discarded.** The
`module_globals` handler emitted the `load` but never set `last_type`, so the
method dispatcher saw an untyped receiver and fell into its silent
`return "0"` — emitting no call at all:

```saffron
// helper.sf
var items: List<Int> = []

// main.sf
import "./helper.sf" as Flag
Flag.items.push(1)
Flag.items.push(2)
IO.println(Flag.items.length())   // printed 0; the pushes emitted no IR
```

RC=0, no diagnostic, the program just did nothing. Fixed by publishing
`global_var_types.get(mp_resolved)` into `last_type` at both handler sites in
`expr_body.sf`. This is the same silent-`return "0"` shape as #37.

**(c) Dependency preludes were parsed without collecting their imports.** The
auto-import loop in `main.sf` read each dependency's `src/prelude.sf`, lexed and
parsed it, and pushed it as a module — but never registered its import aliases or
recursed into what it imported, unlike `collect_modules` for an ordinary import.
An alias used inside a prelude was therefore an undefined variable at codegen.

That is why `turmeric/src/prelude/` hand-wrote the *already-mangled* symbols
`turmeric_signal_effect` and `turmeric_signal__tracking` instead of importing
`signal.sf`. Those are bare `Variable` references, so they never reached the
MemberAccess handler that this entry describes — the earlier note blaming #22 for
turmeric's build failure was right about the symptom and wrong about the cause.
They resolve only when the prelude is auto-imported into an app (where
`turmeric/signal` yields the `turmeric_signal_` prefix); building the library
standalone, where the same module is reached as `./signal.sf`, left them
undefined. The auto-import loop now registers aliases and recurses through
`collect_modules`, and the prelude uses `Reactive.effect()` / `Reactive.untrack()`.

**Worth knowing:** a module alias that collides with a class name in the same
module (`import "./signal.sf" as Signal`, where signal.sf declares
`class Signal`) silently compiles the member access to the class constructor and
**drops the call entirely** — RC=0, no diagnostic, the function body becomes a
no-op. That is why the prelude aliases it `Reactive`. Reproducing this needs the
prelude auto-import path; a plain two-file program with the same shape works, so
it is not yet isolated to a minimal case and is not separately filed.

### 23. FIXED — runtime alias functions declared the wrong static return type, so a String interpolated as an address

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

**Partially patched at the stdlib boundary (2026-07-31).** The specific example
above no longer reproduces: `OS.system(cmd) == 0` works, as do `file_size`,
`file_exists`, `is_dir`, `rename`, `delete_file` and `set_env` through the `@io`
and `@os` module wrappers. Those call sites now route through a `_retag(n) =>
n + 0` helper in `src/lib/io.sf` / `src/lib/os.sf` (BUGS #88). That is one
boundary out of many, and it is the wrong layer — the root cause below is
untouched, every other int-returning runtime function is still affected, and the
helpers exist to be deleted when this is fixed properly. Do not read the working
examples as this bug being closed.

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

**FIXED (2026-08-02) — the defect was the static type, not the tagging.**
`IO.readln()` and `IO.read_line()` are runtime aliases of `__io_readline`, which
was on codegen's hardcoded list of string-returning symbols while its two aliases
were not. So `last_type` stayed `IntType` and interpolation picked
`__int_to_string`: `"${IO.readln()}"` printed `[5721070080]`.

The bytes were never wrong — `__rt_tag_ptr` was applied all along. That is the
general tell worth remembering: `.length()` returned 5 on the very same value
that interpolated as an address. **Length right + interpolation wrong ⇒ static
type bug; both wrong ⇒ tagging bug.**

Fixed by replacing three independent `or`-chains (string / bool / int, each
having to remember every symbol, none covering the remainder) with one
`runtime_ret_discipline(fn)` table returning `"string"`, `"bool"`, `"int"`,
`"list"`, `"void"` or `""`. Naming `"list"` and `"void"` is half the value:
list-returning runtime functions travel deliberately **untagged**, because
`__gc_is_heap_ptr` (`gc.ll:615`) rejects NaN-tagged values as heap pointers.
That was previously encoded as an *absence* from all three chains — indis­tin­guish­able
from an oversight — and is now a stated discipline carrying its reason. Placed at
~line 1146, above both the ~1640 preamble hazard and the prefixed-alias arm.

Output with stdin `hello\nworld`: `readln len=5 val=[hello]` /
`read_line len=5 val=[world]`. Regression test
`test/pass/dispatch_readln_is_a_string.sf`, 3 assertions, stdin-independent (it
holds at EOF).

**Observed, not fixed:** `OS.mkdir()` still returns a raw 0, so `== 0` is false.
It routes through the `stdlib_os_mkdir` Saffron wrapper, a different path from
the builtin dispatch changed here.


### 24. FIXED — the two ends of the `@extern` boundary disagreed on which C types they convert

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


### 25. FIXED — a method called directly on an interpolated string literal returned garbage

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

**Root cause as filed** (wrong, corrected below): interpolation desugars in the
lexer to `"" + (expr).to_string() + ""`; the concatenation produces a raw
`char*`, assigning it to a variable goes through a path that tags it, but calling
a method on the concatenation expression directly passes the untagged pointer as
the receiver. That put it in the same raw-pointer-leak class as #23, with #24's
static `Ptr` distinction as the fix.

**Actual root cause: operator precedence in the desugaring, not tagging.** The
lexer's desugaring is correct as far as it goes, but it emits the token sequence
unparenthesised, and `.` binds tighter than `+`. So `"${n}".length()` parsed as

```
"" + (n).to_string() + "".length()
```

— the method attached only to the *trailing* `""` segment. `"".length()` is `0`,
so the expression is `string + string + 0`, and that int `0` reached `strlen` as
an address. Nothing was mis-tagged; the receiver of `length()` was never the
concatenation at all. This also explains cleanly why the variable-bound form
worked (`.` has nothing adjacent to bind to) and why the garbage looked like a
heap address (it was the result of the *string* concatenation, printed as the
value of the whole expression).

**Resolution (2026-07-31).** `src/compiler/lexer.sf` now wraps the entire
interpolated string in one paren group: `TkLParen` emitted at the first `${`
(guarded on `!has_interp` so it happens once), `TkRParen` after the trailing
segment (guarded on `has_interp`, so a plain `"abc"` opens no group). A trailing
method then binds to the group rather than to the last `""`. Two lines of tokens,
no codegen or checker change, and #24's static `Ptr` distinction is *not* needed
for this bug — it stands or falls on its own merits.

Verified: `"${n}".length()` → 2, `"x${n}y".length()` → 4,
`"${name}".to_upper()` → `WORLD`, and the plain-interpolation text paths
(`"${n}"`, `"hello ${name}!"`, `"${1 + 2} is three"`, `"a${n}b${name}c"`) all
unchanged. Regression test at `test/pass/interp_method_call.sf`, which covers
both the method-call forms and the plain text forms so a fix that breaks ordinary
interpolation cannot pass it. Full bootstrap green including the gen4 fixed
point; test-suite failure set identical to before the change (38, no test
previously exercised this).


### 34. FIXED — `bootstrap.sh` now builds a gen4, and the criterion it contradicted holds

`CLAUDE.md` listed "gen3 can compile itself (bootstrap a gen4 from gen3)" as a
promotion criterion and claimed the TEST stage verified it. It did not — TEST
compiled sample programs. Worse, a hand-built gen4 segfaulted on
`IO.println("hi")`, so the documented criterion had never held.

STAGE 2, added 2026-07-31, closes both halves: it compiles every compiler source
with gen3, links the result, and checks the gen4 can compile a program. Skippable
with `SKIP_GEN4=1` for iteration, which is documented as the one thing not to skip
when deciding a promotion.

Verified 2026-08-01: `build/stage4/saffronc` compiles `IO.println("hi")` and a
class-plus-import program, both rc=0. Adding the stage immediately paid for
itself — it surfaced a one-armed match in `codegen.sf` that gen3 had been
rejecting all along with nobody looking, and it is the stage that would have
caught [[BUGS #100]] had gen4 been an independent check rather than gen3's own
output (see that entry for why it still could not).

### 38. FIXED — a coroutine reached through a module alias was called as a plain function, so `Async.sleep` never suspended and `Async.gather` returned frame handles

Originally filed as "forwarding an `await` through a wrapper function returns the
handle" — that framing was wrong. Nothing about *forwarding* or *wrappers* was
special; the defect was in how a call through a module alias was lowered, and it
was four independent bugs stacked on the same code path. Each one hid the next,
which is why the symptom looked type-related for so long.

**Symptoms as filed:**

```saffron
fun mk_int(): Int { Async.sleep(0.01); return 42 }
var t: Task<Int> = Task.spawn(fun () => mk_int())
IO.println(Async.gather([t]).to_string())   // "105553180263168" — a frame handle
```

Plus a quieter one with no error and no wrong value: `Async.sleep()` returned
instantly, so `Task.spawn`ed bodies ran to completion synchronously inside
`spawn` and *all concurrency silently disappeared*. `test/test_async.sf` printed
`A start / A end / B start / B end` and the suite called it a PASS, because
`run_tests.sh` only checks the exit code.

**Defect 1 — the dispatch arm that swallowed every alias.**
A function containing a suspend point is emitted `define ptr @f(...)
presplitcoroutine` and returns a *frame handle*, not a value. Its callers must
emit a resume loop (`__sched_coro_done` / `llvm.coro.suspend` /
`__sched_coro_resume`) and then read `@__task_result`. `gen_method_call`'s
"Universal module dispatch" arm (`methods_body.sf:1187`) intercepted every alias
with a non-empty prefix and emitted a bare `call i64` without consulting
`coroutine_funcs`. The coro-aware arms further down were therefore **dead code
for `Async.*`** — only empty-prefix builtins (IO/OS/GC) ever reached them. That
single miss produced both filed symptoms: the handle read back as the result,
*and* the callee's suspension never driven, so nothing read `__yield_reason` and
yield reason 1 never reached the scheduler's `sleep_queue`. Fixed by factoring
the loop into `gen_coro_call` and calling it from all three dispatch arms,
including the two `named_imports` arms (`import { sleep } from "@async"`).

**Defect 2 — `get_yield_arg(): Int` turned a Float duration into NaN.**
`__yield_arg` holds an already-NaN-boxed value whose type depends on the yield
reason: a Float duration for 1, an fd for 2, a task handle for 3/5. The `Int`
annotation made `gen_extern_call` re-box it with `__val_tag_int`, and for a Float
that overwrites the exponent bits. `Async.sleep(0.05)` arrived at
`var duration: Float = get_yield_arg()` as **NaN**, so `sleep_times.push(now +
duration)` stored NaN, `NaN <= now` was never true, and the task sat in
`sleep_queue` forever — no error, no resume, program exits. `Int` had survived
only because re-tagging an already int-tagged value is idempotent. Fixed by
annotating it `Any`.

**Defect 3 — `__val_untag_int` truncated every coroutine handle to 0.**
The `from_float` fallback in `base_nanbox.ll` handled three input shapes
(NaN-boxed, raw small int, tagged double) and missed a fourth: a **raw
pointer**. A macOS heap address (~`0x6000_0000_0000`) is neither NaN-boxed nor
"small", so it fell through to `bitcast`-to-double — a denormal — and `fptosi` of
a denormal truncates to **0**. `__sched_coro_done(0)` then answers "done" via its
null guard, so `scheduler_tick` retired all three tasks of a two-task program
without resuming any of them. Fixed by returning the value untouched when the
exponent field is zero: a genuine Float never lands there with a zero exponent,
and a true denormal (|x| < 2.2e-308) converts to 0 anyway.

**Defect 4 — an `Any` extern *parameter* was untagged on the way in.**
`gen_extern_call` respected an `Any` *return* annotation (pass the payload
through untouched) but had no equivalent on the parameter side: every i64 param
was untagged. The scheduler's `store_result(handle, value)` therefore untagged a
tagged 42 down to a raw 42, put that in the C result table, and
`__sched_get_stored_result` handed it back untagged — so `task.await() == 42` was
**false** for a task returning 42, while `.to_string()` still printed "42"
(untag tolerates a raw int). A Float result was truncated outright. Fixed by
recording each extern's Saffron parameter types in `extern_param_types` and
passing `Any` i64 params through untouched, mirroring the return path.

**Regression test:** `test/pass/async_module_coro_call.sf` — asserts `gather`
returns the awaited values, that two sleeping tasks interleave (`start 1` then
`start 2`, the observable proof that sleep suspends), and that a suspending
function called from sync code still returns its value.

**Suite blind spot this exposed:** `tools/run_tests.sh` only checks exit code 0.
`test_async.sf` was green for the entire life of this bug while emitting 2 of its
~12 expected lines. Filed separately as #90.

**Follow-on, now also fixed:** `Async.await` did not exist in
`src/lib/async.sf`, though `CLAUDE.md` documented it and both
`src/lib/promise.sf:10,26` and `test/async_coop.sf:16-18` called it — they failed
to link (`_stdlib_async_await` undefined), which is why `async_coop` was at exit
1. Adding it was blocked on this bug precisely because it would have linked and
then silently returned frame handles. Added as a one-line forward to
`task.await()`; `async_coop` and `Promise.all` both work now. Covered by
`test/pass/async_await_function.sf`.

**FIXED (2026-08-02) — the two ends of the boundary now agree on which C types
they convert.** The immediate fix this entry proposed (untag `i64` params) had
already landed in `f758f42`. The residual defect was the same asymmetry in
general form: the parameter loop and the return path each ended in a bare `else`
assuming "anything unrecognised is already an i64", and the two `else`s covered
**different sets of C types**. The param side handled `i64`/`i8*`/`i8**`/`i32`/
`double`; the return side handled `void`/`i8*`/`i32`/`double`/`i64` — no `i8**`.

So three declaration shapes silently emitted non-assembling IR: a `void**`
return produced `__val_tag_int(i64 %t8)` applied to an `i8**`; an `i1` return did
the same to an `i1`; and `i16`/`float` params emitted `i64 %v` against a
contradicting `declare`.

Each end now names the C types it can convert
(`extern_param_type_supported` / `extern_ret_type_supported`) and raises a
`has_errors` diagnostic quoting the offending signature otherwise. Deliberately
**not** more conversions: a `float` param needs an `fptrunc`, an `i8**` return
needs a real box, and each is its own tagging question that should be decided
explicitly rather than by a fall-through.

The three bad shapes went from an opaque `opt` type error against generated IR
(`'%t8' defined with type 'ptr' but expected 'i64'`, reported to the user as
"this is a compiler bug") to a named diagnostic at the declaration. Working cases
are unchanged: `strlen("hello")` = 5, `sqrt(16.0)` = 4, `llabs` on Float- and
Int-typed 64 = 64, `malloc`/`free`, and the `Any` i64 roundtrip still `== 42`.

Swept all **46 files** in the tree declaring an `@extern` (175 return and 291
parameter declarations): **zero false positives**. Negative tests
`test/fail/dispatch_extern_unconvertible_param.sf` and
`test/fail/dispatch_extern_unconvertible_return.sf`.


### 37. FIXED — method dispatch that matched no branch deleted the call and returned a silent zero

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

**FIXED (2026-08-02) — with the measurement the entry kept asking for.** The
terminal fall-through in `gen_method_call` is now a diagnostic. It set
`last_type = IntType` and returned the literal `"0"` *without emitting a call*,
so the method call was not mis-compiled, it was **deleted**: clean compile, no
crash, wrong answer.

The measurement this entry demanded was run first, with an instrumented gen3
logging every fall-through with its category and receiver type:

| Corpus | Files | Unreported fall-throughs |
|---|---|---|
| `test/`, `test/pass/`, `test/fail/`, `src/lib/` | 260 | **0** |
| `turmeric`, `parsley`, `basil`, `pantry`, `examples` | 66 | **2** |
| compiler's own 5 TUs (identity mode) | 5 | **0** |
| **total** | **332** | **2** |

Coverage was checked rather than assumed: 258 of 332 files actually reach
codegen — 28 die in the parser, 15 in the checker, 7 on unresolved imports, and
10 fail *in* codegen and so were measured.

Because the radius is 2 files rather than hundreds, the staged rollout this entry
contemplated was unnecessary and the full fix landed at once. A/B over all 325
compilable files: **exactly 2 change accept→reject**, and both are the same real
bug — `manifest.dependencies.delete(name)` on a `Map<String, Any>` in
`pantry/src/commands/remove.sf` and `pantry/src/main.sf`. `Map` has no `delete`;
**there is no `__map_delete` in `src/runtime/runtime.sf` at all**. A Map receiver
is classified builtin, so the old user-class-only check skipped it, and
`pantry remove <pkg>` compiled clean, emitted **zero `__map_*` instructions**,
and removed nothing. No test covered it.

Why the count is so low is itself the finding. It is not that this rarely
happens — it is that the arms above are permissive to a fault. The prefixed-alias
arm matches almost any receiver, and that same over-matching is what makes
specialized arms below it dead code (#38, #91). One design problem, now measured
from two sides.

**Not fixed, and now a hard error rather than a silent no-op:** `Map.delete`
still does not exist. Erroring is the correct first move, since the call never
did anything, but `pantry/src/commands/remove.sf` now needs either a
`__map_delete` helper in `src/runtime/runtime.sf` plus the four `.ll` bases, or a
rewrite of that one call site.

Negative tests: `test/fail/dispatch_dropped_call_on_any.sf` and
`test/fail/dispatch_dropped_call_on_map.sf` — both rejected (rc=1) by the
bootstrapped gen3, both **accepted** (rc=0) by the previous compiler.

This is the fourth of five instances of one pattern: a resolution helper that
cannot fail, so it guesses instead of reporting not-found (#22, #40, #78, #37,
#103).


### 112. FIXED — a range literal `0..5` lexed as `0.` `.` `5` and silently compiled to a list index

There is no range syntax. `TkDot` is the only dot token; `grep -n "Range\|DotDot"`
finds nothing in `lexer.sf`, `parser.sf` or `ast.sf`. What happens instead is that
the number scanner (`lexer.sf:294-301`) consumes one trailing `.` as a decimal
point, so `0..5` tokenizes as **`TkFloat(0.)` `TkDot` `TkInt(5)`**. `parse_call`
(`parser.sf:597-606`) then takes its numeric-dot-access branch — the one meant for
`tuple.0` — and builds `IndexGet(FloatLit(0.), IntLit(5))`.

Emitted IR for `var r = 1..5`:

```llvm
%t2 = call i64 @__val_tag_float(double 1.)
%t7 = call i64 @__list_length(i64 %t2)   ; length of a *float*
%t10 = call i64 @__list_get(i64 %t2, i64 %t9)
```

A float is passed to `__list_length` / `__list_get` as if it were a list. The only
output is `[checker] Warning: r: cannot infer type` — **no diagnostic names the
real problem** at any stage: not the lexer, not the parser, not the checker, not
codegen.

Runtime behaviour depends on the left operand: `0..5` gives
`Runtime Error: NullError`; `1..5` **segfaults**. Annotating
(`var r: List<Int> = 0..5`) does not help — the checker accepts it. Hand-writing
`0. . 5` reproduces it exactly, confirming the token sequence.

**This is the root cause of the existing suite failure `test/pass/ranges.sf`**
(`Runtime Error: NullError: null pointer dereference`). That file is four range
literals and nothing else; its first line is `var nums = 0..5`.

Two defensible fixes: reject `Float . Int` in `parse_call`'s numeric-dot branch
(the receiver can never be a tuple), or have the number scanner refuse a `.`
followed by another `.`, which also leaves room for real range syntax later.

Note this is a *syntax* gap wearing a codegen costume. The language has no ranges;
the defect is that asking for one produces a running program instead of a parse
error.

**FIXED (2026-08-02)** with two independent guards, and the pair is not
redundant — each catches a case the other cannot.

`read_number` (`src/compiler/lexer.sf:294`) no longer treats a `.` as a decimal
point when a second `.` follows, so `0..5` scans as `TkInt(0) TkDot TkDot
TkInt(5)` — which is also the token stream a real range operator would want, so
the fix does not foreclose one. `parse_call` (`src/compiler/parser.sf:597`)
rejects `..` by name, and *separately* rejects `<float literal> . <int>` because a
tuple-access receiver can never be a float.

The second guard is what makes the lexer fix sufficient: `1.5.0` and a
hand-written `0. . 5` scan **correctly** and still reach the tuple branch, so the
lexer change does not touch them. Conversely the lexer fix is what makes the
messages good — with only the parser guard, `0..5` would be blamed on its float
literal rather than named as `..`. The `..` guard fires first, before the dot is
consumed, so neither diagnostic swallows the other:

```
[line 1, col 10] Error: '..' is not an operator - Saffron has no range syntax; build a list or use a while/for loop
[line 1, col 13] Error: cannot use '.' field access on a float literal - a '.' right after a number is its decimal point
```

Verified: both spellings compiled with **exit 0** before and exit 1 after — the
prior behaviour was `[checker] Warning: r: cannot infer type` and nothing else,
then NullError for `0..5` and a segfault for `1..5`.

Float lexing does not regress, which was the real risk. `1.5 0.5 1. 1.0 1.5e3
1e3 1.5E-2 0x1F 3.14159 9.` and `(1.5).floor()` produce **byte-identical output**
before and after. A compile-exit-code sweep over all 290 `.sf` files in `test/`,
`test/pass/`, `test/fail/`, `src/lib/`, `src/compiler/` and `src/runtime/` differs
in exactly two files, both of them the new fail-tests; the new diagnostic fires on
one pre-existing file only, `ranges.sf`.

Regression tests: `test/fail/ranges.sf` — `git mv`'d out of `test/pass/`, where it
had been asserting a feature that never existed — and `test/fail/float_literal_dot_int.sf`
for the case the lexer change does not cover.

---

### 113. FIXED — over-applying a zero-arity function was silently accepted, because the arity check exempted arity zero

`expr_body.sf:2302` and `:2308` both guard with
`args.length() != expected and expected > 0`, and **`expected > 0` exempts every
0-arity function from arity checking entirely**:

```saffron
fun c() { IO.println("in c") }
c("too", "many")        // compiles clean, rc=0, no diagnostic
```

Verified by bisection that only `expected == 0` slips through: `f(1,2,3)` against a
1-arity function and `g()` against a 2-arity one both produce
`[codegen] Error: 'f' expects 1 arguments, got 3`.

The `expected > 0` clause reads as a guard against an *unrecorded* arity — a map
lookup that answered 0 because nothing was registered — but it is written as a
test on the arity value itself, so it cannot distinguish "takes no arguments" from
"we never found out". Fix: drop `expected > 0` and guard on whether an arity was
recorded at all, which both `has()` calls immediately above already establish.

This makes `test/functions.sf` hang, which is how it was noticed.

Same family as #22, #40, #78, #37 and #103 — a check that cannot express "unknown"
so it conflates unknown with a legitimate value. Here the conflated value is zero,
which is also the sentinel.

**FIXED (2026-08-02).** Verified: `fun c() {}; c("too","many")` compiled with exit 0
and no diagnostic before; now `[codegen] Error: 'c' expects 0 arguments, got 2` and
exit 1. Correct 0-arity calls still work.

The fix consults `func_param_count` **first** and **without** the `expected > 0`
clause, demoting `called_function_arity` to a fallback that keeps it. The reason the
clause cannot simply be deleted from both arms is a provenance asymmetry between the
two maps, and it is the interesting part:

- `func_param_count` is written **only** by declaration sites (`gen_fun_decl` plain
  and `@inline`, `gen_class_method`, `prescan_fun_decl`, `prescan_class_decl`). A `0`
  there *is* a declared arity.
- `called_function_arity` is overloaded. Declarations write it, but so do **call
  sites**, storing `args.length()` (`methods_body.sf:1861`, `:3113`, `:1399`,
  `:1795`) purely to size the `declare i64 @f(...)` lines `emit_module` emits for
  undefined callees (`output_body.sf:1258`). A `0` there cannot have come from a
  declaration, so it genuinely means *unknown*.

So `expected > 0` was a legitimate unknown-guard for one map and pure damage applied
to the other. Written as a test on the arity **value** rather than on whether an
arity was recorded, it exempted every 0-arity function in the language.

Worth recording that the naive fix would have worked *today*: a variant compiler with
`expected > 0` dropped from both arms produced **byte-identical diagnostics** across
285 files, so no unrecorded-arity case is currently reachable. The exemption was kept
anyway, because the call-site writers make a `0` in that map unfalsifiable — if a
`gen_method_call` path ever reaches this check, the clause is what stops a leftover
`0` from fabricating "expects 0 arguments" against a 3-arg function. Measured-dead,
retained deliberately, and now documented in place so the next reader does not have
to re-derive it.

Diagnostics-only, and this is the load-bearing evidence: over 321 files (`test/`,
`test/pass/`, `test/fail/`, `src/lib/`, `src/compiler/`, `examples/`, and five more
fixture trees), **exactly one** behavioural change — the new negative test — and all
225 emitted `.ll` files byte-identical.

Provenance: added in `efd9f47`/`cc9b466` (2026-05-28) as a *Warning* and later
promoted to an error without the clause being re-examined. Seventh instance of the
can't-express-unknown family (#22, #40, #78, #37, #103, #114), and #118 makes eight.

**This bug is also why `test/functions.sf` hung.** The file carried
`fun c() { c("too","many") }` as a Crafting Interpreters stack-trace test, inherited
from the C VM where over-application was a *catchable runtime error*. Under the
native compiler nothing rejected it, so it simply recursed until the 10s timeout
(`rc=124`). The compile-time half is now `test/fail/arity_zero_overapplied.sf`, with
`test/pass/arity_zero_ok.sf` covering seven correct 0-arity shapes.

---

### 114. FIXED — a two-parameter function type was split in half, manufacturing a phantom parameter, so `reduce` silently returned the wrong answer

**Severity: critical while live.** A wrong answer in shipped stdlib, with no error
and no warning.

`codegen.sf`'s `split_respecting_generics` tracked `<>` nesting but **not** `()` or
`[]`. Meanwhile `type_to_string` renders a `FuncType` via `func_type_string` as
`Fun(A,B):R` with a **plain comma**, and `params_to_string` joins parameters with a
plain comma too. So `gen_function` split a two-parameter function type down the
middle and manufactured a phantom parameter named `Int)`, which `sanitize_name`
turned into `%Int_`:

```
fun arrow_two(b: Box, n: Int, f: (Box, Int) => String)
"b:Box,n:Int,f:Fun(Box,Int):String"
  -> [b:Box] [n:Int] [f:Fun(Box] [Int):String]      4 params, not 3
```

The phantom lands **between** the real parameters, so every parameter after it is
read from the wrong slot. `src/lib/iter.sf:75` declares
`reduce(iterable, func: (R, T) => R, initial: R)`, so `initial` shifted:
`reduce([1,2,3,4], (a,b) => a+b, 0)` answered **12** instead of **10**.

Confirmed by running the two compilers side by side — `reduce=10` before the
parser change, `reduce=12` after it and before this fix.

**Latent, not introduced.** Before the arrow-type parser fix (#86's other half)
nothing put a *populated* `FuncType` into a parameter node from that path, so the
`Fun(A,B):R` form never appeared there and the paren-blindness was harmless. It
became reachable the moment the parser fix landed, which is why the two had to
merge as one unit — the parser fix alone silently corrupts stdlib. That is also
how it was caught: the agent that wrote the parser fix refused to merge it and
reported this instead.

Fixed by adding `(` and `[` to the depth tracking. One subtlety worth recording:
unlike `parser.sf`'s `split_type_args`, a closer only decrements a depth that is
**already positive**. The split is gated on `depth == 0`, so an unbalanced `)`
would otherwise drive depth negative and silently disable **all** splitting for
the rest of the string — worse than the bug being fixed. Clamping degrades to the
old behaviour instead.

Swept the whole stdlib for other victims. Only `reduce` has a comma *inside* its
arrow parentheses; `map`/`filter`/`any`/`all`/`each`/`count`/`find`/`flat_map` are
all single-parameter, so no split occurred. Verified all nine `iter` functions
produce byte-identical output before and after, and separately checked 1-, 2- and
3-parameter arrow types each followed by further parameters. Regression tests:
`test/pass/iter_arrow_param_split.sf` and
`test/pass/checker_block_param_arrow_type.sf`.

---

### 120. FIXED — calling a `Fun`-typed field through `this` emitted a call to a nonexistent method symbol

A class field holding a closure could not be called as `this.field(args)`. The
program failed to **link**, with an undefined symbol `Class__field` — the mangling
for a *method* named `field`, which no definition exists for:

```saffron
class Holder { var f: Fun
               fun init(f: Fun) { this.f = f }
               fun call_it(x: Int): Int { return this.f(x) } }
Holder(fun (x: Int): Int => x * 2).call_it(21)
```

```
Undefined symbols for architecture arm64:
  "_Holder__f", referenced from:
      _Holder__call_it in output-038fc0.o
```

`src/lib/heap.sf` was a casualty: it declares `var _cmp: Fun` and calls
`this._cmp(...)` at heap.sf:101,121,124, so the shipped `@heap` module **could not
be linked at all**.

`gen_namespace_call` (`methods_body.sf:1001`) already had an arm that rewrites a
function-typed-field call into an indirect call through the stored closure pair.
Its guard tested the rendered field type with `starts_with("Fun(")` plus two
arrow-shaped spellings. The trap is that the three ways to declare a
function-typed field parse to three **different AST nodes** and therefore render as
three unrelated strings:

| Spelling | AST node | Rendered |
|---|---|---|
| `(A, B) => R` | `FuncType` | `"Fun(A,B):R"` |
| `Fun<A, B, R>` | `GenericType("Fun", ...)` | `"Fun<A,B,R>"` |
| `Fun` | **`ClassType("Fun")`** — not a function-type node at all | `"Fun"` |

Only the arrow spelling matched. Bare `Fun` is the one that should be surprising:
it never becomes a `FuncType`, so `type_to_string` returns it through the
`ClassType(n)` arm as the plain string `"Fun"`, with no bracket of any kind to
match on. The other two fell through to the ordinary method path below, which
mangles `ns + "__" + method` and emits a direct call.

Because the mistake produced a *reference to a symbol* rather than wrong
arithmetic, it surfaced at link time instead of as a silent miscompile — which is
the only reason it was not much worse.

Verified directly, and the negative evidence is the good part: a three-class probe
gave `_BareFun__f` and `_GenericFun__f` undefined while **`_ArrowFun__f` was
absent** from the linker's list — exactly the spelling the old guard handled. After
the fix all three print correctly.

**Copying the field to a local first worked**, and was the de-facto workaround:

```saffron
fun call_it(x: Int): Int { var g: Fun = this.f
                           return g(x) }        // prints 42
```

A local in callee position goes through `gen_call`'s indirect path, which unpacks
the closure pair directly and never consults a field type at all. That the
workaround existed is why this read as a dispatch bug rather than a broken
closure-invocation path — the invocation machinery was fine throughout.

Fixed by adding `Fun<` and exact `"Fun"` to the guard (`methods_body.sf:1021`), so
all three spellings take the indirect-call arm. The new arm was verified to
actually *execute* rather than inferred from a passing test — `Holder__call_it` now
emits `inttoptr` + `call i64 %t11(i64 %t9, i64 %t10)`, unpacking both slots of the
closure pair, and `Holder__f` appears 0 times in the IR. That check matters in this
file specifically: the universal prefixed-alias arm makes any specialized arm below
it dead code, which is what caused #38 and #91.

`test/pass/fun_field_call.sf` covers all three spellings in both the
`this.field(args)` and local-copy forms, plus a capturing closure (so slot 1's env
pointer must be loaded, not just the code pointer), a class with two function fields
and one non-function field (so the `getelementptr` index must be right rather than
incidentally zero), a class where a real method of the same shape must still
dispatch statically, and `@heap` driven through its actual import.

---


## Fixed

- ~~#53: `UUID.v4()` always returned the all-zero UUID~~ — `src/lib/uuid.sf`
  builds its string from `_to_hex(Random.int(0, 15))`, and `_to_hex` indexes a
  16-element list of hex digits. `random.sf` declared its whole surface `Float`,
  including `_rand()` (whose C signature is `i32 rand()`) and `int(min, max)`,
  whose results are used as list indices. Indexing with a `Float`-typed value
  reinterprets the double's bits as an integer instead of converting it (#52), and
  for small values the low bits are zero — so every index landed on element 0 and
  `UUID.v4()` returned `00000000-0000-4000-8000-000000000000` on every call, a
  constant where callers expect uniqueness (request IDs, temp file names, database
  keys). Fixed in `b75689b` by declaring the integer-valued surface as `Int`
  (`_rand`, `_srand`, `seed`, `int`, and the index/count locals in `choice`,
  `shuffle`, `sample`); `float()` stays `Float` since it genuinely returns a
  fraction. `test_uuid` passes, and `choice`/`shuffle`/`sample` were verified.

  This is a symptom fix at the honest-signature level. **#52 — the `Float`-index
  reinterpretation itself — remains open and is the real defect;** any other
  stdlib or user code that indexes with a `Float` still reads element 0 silently.

- ~~#58: `is Float` was true for every Map, List, and instance; `is Number` was
  false for every float~~ — two halves of one defect. `__val_is_float` in
  `base_nanbox.ll` defined "float" as "none of the three NaN-box tags", but a
  heap object passed through an `Any` binding arrives as a **raw untagged GC
  pointer** — top bits clear, so no tag matches, so it was called a float.
  `is Number` masked this because it lowered to `__val_is_int`, a *positive*
  tag match, correctly false for a pointer — but for the same reason `is Number`
  answered false for every genuine float, so all seven `is Number` guards in the
  stdlib (each a "numeric? then format it" branch) were silently falling through
  to their default for float input. Rewriting them to `is Int or is Float`
  exposed the pointer half: `JSON.to_string({"name": "hello"})` took the number
  branch and printed `5502959640`. `__val_is_float` now mirrors what
  `__val_is_list`/`__val_is_map` already did on this base — when no tag matches
  and the top 16 bits are clear, probe the GC magic sentinel at `v-8` before
  concluding "float". Only `base_nanbox.ll` needed it; the other three IR bases
  keep collections PTR-tagged, and their own `__val_is_list` checks only the PTR
  tag, which confirms raw pointers never reach a type check there.
  `test_regressions` 44/44 (was 40/44). Found by running the full suite after a
  stdlib change — the `opt -verify` gate alone would not have caught it, since
  the IR was well-formed and merely wrong.

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
