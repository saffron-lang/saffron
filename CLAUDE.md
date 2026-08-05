# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What is Saffron

Saffron is a statically typed scripting language with a **self-hosted compiler written in Saffron**. It compiles source to LLVM IR via an AST intermediate representation, then links to a native binary or a wasm32 module. The runtime provides a garbage collector, NaN-boxed values, and cooperative async built on LLVM coroutines.

> A stack-based bytecode VM written in C used to be the implementation. It now lives in `legacy/` and is **dead and unsupported** — it has drifted far enough behind the language that it can no longer parse the compiler's own source. Do not use it, cite it, or tell users about it.

## Build

```bash
# Bootstrap: gen2 (checked-in) compiles gen3 from source
./bootstrap.sh
```

There is no full rebuild from scratch. The checked-in gen2 at `build/stage2/saffronc` is the sole root of trust for the bootstrap chain; if it breaks, recover it from git history. (`--full` used to rebuild it from the C VM and now fails with that explanation.)

The native compiler is at `build/saffronc`. Run a file with:

```bash
build/saffronc input.sf output.ll                    # compile to LLVM IR
tools/saffron run program.sf                         # compile + link + run
tools/saffron build program.sf -o app                # compile to native binary
tools/saffron build program.sf --target wasm32 -o app.wasm   # compile to wasm32
tools/saffron emit-ir program.sf                     # print LLVM IR to stdout
```

There is currently **no REPL** — the only one was the C VM's. Use `tools/saffron run` for quick iteration.

wasm32 linking requires Homebrew LLVM (`/opt/homebrew/opt/llvm/bin/clang` + `/opt/homebrew/bin/wasm-ld`); Apple's system clang cannot target wasm32 at all.

## Architecture

The pipeline is: **Lexer → Parser → Type Checker → LLVM IR Codegen → clang/wasm-ld**

### Key layers

- **Lexer** (`src/compiler/lexer.sf`) — tokenizes source into `Token`s
- **AST** (`src/compiler/ast.sf`) — node definitions as enums
- **Parser** (`src/compiler/parser.sf`) — builds the AST; also desugars `for-in`
- **Type Checker** (`src/compiler/checker.sf`) — validates types before codegen
- **Codegen** (`src/compiler/codegen/`) — emits LLVM IR, split across `expr`, `stmts`, `methods`, `match`, `closures`, `types`, `intrinsics`, `output`, `utils`
- **Diagnostics** (`src/compiler/diag.sf`) — error reporting
- **Runtime** (`src/runtime/`) — `runtime.sf` (Saffron-level runtime, incl. NaN-box tagging), `gc.ll`, and four IR bases: `base.ll` (bootstrap), `base_nanbox.ll` (native), `wasm_base.ll` (wasm64), `wasm_base_32.ll` (wasm32). Host-only C helpers: `async_native.c`, `socket_native.c`, `watch_native.c`, `process_native.c`
- **Standard library** — Saffron stdlib in `src/lib/`

The `src/compiler/codegen/*_body.sf` files are sed-assembled into `codegen.sf` at
its `@codegen-split:` markers, so they hold the *class body*. But `codegen.sf`
itself is live source, not a skeleton: everything past the class's closing brace
(currently line 557) is top-level free functions that sed copies through
verbatim, and that tail alone holds ~58 AST pattern-match sites. When you change
an AST node's shape, `codegen.sf` needs the same sweep as every `*_body.sf`.
There are no inactive mirrors in `codegen/` any more — the non-`_body` copies
were deleted, and `bootstrap.sh` now fails the assembly if a `*_body.sf` has no
marker or a marker has no file.

### Adding a language feature (the full workflow)

1. **Syntax**: new tokens in `lexer.sf`, new AST variants in `ast.sf`, parsing in `parser.sf`
2. **Types**: type rules in `checker.sf`
3. **Codegen**: IR emission in the relevant `codegen/*_body.sf`
4. **Runtime**: new helpers in `src/runtime/runtime.sf` or the `.ll` bases (all four, if the feature is target-independent)
5. **Stdlib**: importable `.sf` files in `src/lib/`

Then follow the bootstrap constraints below — you cannot use new syntax in compiler source until gen2 understands it.

### Value representation

Values are **NaN-boxed** i64s: `TAG_PTR 0x7FF8`, `TAG_INT 0x7FF9`, `TAG_SPEC 0x7FFA` (nil/true/false). Doubles are stored unboxed. Tagging happens in `src/runtime/runtime.sf` via `__rt_tag_ptr` — **never in codegen, and never in both**. All `extern` parameters must be untagged.

## Language Features

### Variables and Types

```saffron
var x: Number = 5
var name = "saffron"       // type inferred
let [a, b, c] = [1, 2, 3] // destructuring
```

### Functions and Lambdas

```saffron
fun add(a: Number, b: Number): Number {
    return a + b
}

var double = fun (x: Number) => x * 2
```

### String Interpolation

```saffron
var name = "world"
IO.println("hello ${name}!")
IO.println("${1 + 2} is three")
```

### String Methods

```saffron
"hello".length()          // 5
"hello world".split(" ")  // ["hello", "world"]
"  hi  ".trim()           // "hi"
"hello".contains("ell")   // true
"hello".starts_with("he") // true
"hello".replace("l", "L") // "heLLo"
"abc".to_upper()          // "ABC"
"hello".slice(1, 3)       // "el"
"hello".index_of("lo")    // 3
"ha".repeat(3)            // "hahaha"
"123".to_number()         // 123
```

### Enums and Pattern Matching

```saffron
enum Option {
    Some(value: Number),
    None
}

var x = Option.Some(42)
var result = match (x) {
    Some(v) => v * 2
    None => 0
}
```

### Destructuring

```saffron
let [head, *middle, tail] = [1, 2, 3, 4, 5]
// head = 1, middle = [2, 3, 4], tail = 5

let Some(value) = Option.Some(42)
// value = 42
```

### Control Flow

```saffron
// For-in is index-based (desugars to length() + an element read), NOT
// protocol-based. Works over Lists, Strings (char by char) and Maps (yielding
// a [key, value] pair). Custom types are a compile error — "type 'X' has no
// method 'length'" — because there is no iterator protocol (BUGS #62).
for (item in [1, 2, 3]) {
    if (item == 2) continue
    IO.println(item)
}

// Break
for (x in items) {
    if (x > 10) break
}

// While, for (C-style), if/else — standard
```

### Exception Handling

```saffron
try {
    throw "something went wrong"
} catch (e) {
    IO.println("caught: ${e}")
} finally {
    IO.println("cleanup")
}

// Runtime errors are NOT catchable — they are fatal.
// IndexError, DivisionError and NullError all route to
// __runtime_error_fatal (src/runtime/runtime.sf:713), which prints to fd 2
// and calls rt_exit(1). The catch block below never runs:
try {
    var list = [1, 2, 3]
    list[99]              // prints "Runtime Error: IndexError: ..." and exits 1
} catch (e) {
    IO.println("caught: ${e}")   // unreachable
}
```

`try`/`catch`/`finally` works for `throw` — only runtime faults are
uncatchable. Nil misuse is generally rejected at compile time by the checker
rather than surfacing at runtime at all.

### Classes and Inheritance

```saffron
class Animal {
    var name: String
    fun init(name: String) {
        this.name = name
    }
    fun speak() {
        IO.println("...")
    }
}

class Dog extends Animal {
    fun speak() {
        IO.println("Woof!")
    }
}
```

### Interfaces

```saffron
interface Printable {
    fun to_string(): String
}
```

### Cooperative Async

```saffron
import "@async" as Async

fun worker(name: String, duration: Number) {
    Async.sleep(duration)
    return "${name} done"
}

var task = Task.spawn(fun () => worker("A", 0.1))
var result = Async.await(task)
```

### Actors

Actors provide serialized access to mutable state across concurrent tasks. Only one method executes at a time per actor instance.

```saffron
import "@async" as Async

actor Counter {
    var count: Int
    fun init() { this.count = 0 }
    fun increment() { this.count = this.count + 1 }
    fun get(): Int { return this.count }
}

var c = Counter()

// Concurrent calls are serialized — no data races
var t1 = Task.spawn(fun () => c.increment())
var t2 = Task.spawn(fun () => c.increment())
t1.await()
t2.await()
// c.get() == 2, guaranteed

// Self-calls (this.method()) execute synchronously — no deadlock
```

Actors are a soft keyword (like `interface`). On WASM targets, actor methods compile as synchronous calls (single-threaded, no contention possible).

### Modules and Imports

```saffron
import "time" as Time           // builtin C module
import "@iter" as Iter          // stdlib .sf file (src/lib/iter.sf)
import "../other/file.sf" as M  // relative path
```

The `@` prefix resolves to `src/lib/<name>.sf` relative to the executable.

### Maps

```saffron
var m: Map<String, Int> = {"a": 1, "b": 2}
m.set("c", 3)
m.get("a")       // 1  (typed Int|Nil — see the `is` caveat below)
m.has("b")       // true
m.keys()         // ["a", "b", "c"]
m.values()       // [1, 2, 3]

// Iteration yields a [key, value] pair per entry, in insertion order.
for (entry in m) {
    IO.println("${entry[0]} = ${entry[1]}")
}
for ([k, v] in m) {         // the array pattern destructures the pair
    IO.println("${k} = ${v}")
}
```

The pair is typed `List<Any>`, not `List<K|V>` — `is` is broken on unions (BUGS
#69), so a union element type would type check and then take the wrong branch.
`m.keys()` and `m.values()` are consistently ordered if you prefer to walk them
by index.

`m.get(k)` returns `Int|Nil`, and `is` is broken on union types: `v is Int` and
`v is Nil` both evaluate false, so those branches are silently dead (BUGS #69).
Use `v != nil`, the one guard that both narrows and evaluates correctly.

### Lists

```saffron
var list = [1, 2, 3]
list.push(4)
list.pop()
list.length()    // 3
list.reverse()
list.sort()
list.copy()
list[0]          // 1 (negative indexing: list[-1] = last)

// Iteration
for (item in list) { ... }
```

## Tests

`tools/run_tests.sh` runs the suites through the real compile+link pipeline. It delegates building to `tools/saffron build` rather than reimplementing the link line.

```bash
tools/run_tests.sh              # all suites (network + known-stale tests skipped)
tools/run_tests.sh main         # only test/*.sf — smoke / feature tests
tools/run_tests.sh pass         # only test/pass/*.sf — must compile AND run
tools/run_tests.sh fail         # only test/fail/*.sf — the compiler MUST reject these
tools/run_tests.sh --network    # also run the network-dependent tests
tools/run_tests.sh -v           # print the captured log for every failure
```

A file in `test/fail/` that compiles cleanly is itself a failure.

Run one test directly:

```bash
tools/saffron run test/<name>.sf
```

### Test stdlib (`@test`)

```saffron
import "@test" as T
T.assert_eq(1 + 1, 2, "basic math")
T.assert(true, "truth")
T.summary()
```

### Named Imports

```saffron
import { map, filter, reduce } from "@iter"

var doubled = map([1, 2, 3], fun (x: Number): Number => x * 2)
```

### Operator Overloading

Classes can define `add`, `sub`, `mul`, `div`, `mod`, `lt`, `gt`, `eq` methods to overload operators:

```saffron
class Vec2 {
    var x: Number
    var y: Number
    fun init(x: Number, y: Number) { this.x = x; this.y = y }
    fun add(other: Vec2): Vec2 { return Vec2(this.x + other.x, this.y + other.y) }
}

var c = Vec2(1, 2) + Vec2(3, 4)  // Vec2(4, 6)
```

### First-Class Types

All types are first-class at runtime. `is` checks work on primitives:

```saffron
42 is Number      // true
"hi" is String    // true
nil is Nil        // true
```

Number and Bool have methods: `(-5).abs()`, `(3.7).floor()`, `true.to_string()`

### Generic Enums

```saffron
enum Result<T, E> {
    Ok(value: T),
    Err(error: E)
}
```

### Interface Conformance

Interfaces with abstract methods enforce implementation. Default methods are inherited:

```saffron
interface Drawable {
    fun draw(): String              // abstract — must implement
    fun description(): String {     // default — inherited
        return "a drawable"
    }
}

class Circle extends Drawable {
    fun init() {}
    fun draw(): String { return "O" }  // required
}
```

Multiple inheritance: `class Duck extends Flyable, Swimmable, Walkable { ... }`

Every base contributes methods, and `is` answers true against each of them. Until
BUGS #111 the parser discarded parents 2..n, so this form silently meant `extends
Flyable`; `test/pass/multi_inherit.sf` is the regression test.

**Only the first base contributes fields**, and only its `init` is forwarded. That
is a deliberate limit, not an oversight: inherited field access and `init`
forwarding work without a vtable because parent field index i equals child field
index i, which can hold for exactly one base. Put the state-carrying class first
and interfaces after it. A secondary base that declares fields is a compile error
rather than a silent misread of the primary base's slots.

### is-Pattern Matching

```saffron
var sound = match (animal) {
    is Dog(d) => d.bark(),
    is Cat(c) => c.meow()
}
```

## Bootstrap & New Syntax

The self-hosted compiler has a chicken-and-egg constraint: gen2 (`build/stage2/saffronc`) must be able to compile the current source. You cannot use syntax in the compiler source that gen2 doesn't support.

### Adding new syntax (safe workflow)

1. **Implement** the new syntax in parser/codegen using ONLY constructs gen2 already handles
2. **Bootstrap**: `./bootstrap.sh` — gen2 compiles your source into gen3
3. **Test gen3** can compile programs that USE the new syntax: `build/saffronc test_new_feature.sf out.ll`
4. **Run end-to-end**: `tools/saffron run test_new_feature.sf`
5. **Do NOT** use the new syntax in compiler source yet — gen2 can't parse it

Only after promoting gen3 to gen2 (see below) can you use the new syntax in the compiler itself.

### Promoting gen2

Promotion copies a working gen3 into `build/stage2/saffronc`, enabling new syntax in compiler source.

**Criteria** — ALL must pass:
- `./bootstrap.sh` completes. Stage 1 is gen2 → gen3; **stage 2** is gen3 → gen4,
  which compiles the compiler's own source with gen3, links the result, and checks
  that gen4 can compile a program. That fixed-point check is the one that matters
  — stage 1 only proves *gen2* accepts the source, and gen2 is not the compiler
  anyone runs. `SKIP_GEN4=1` skips stage 2 (it roughly doubles bootstrap time,
  1m52 → 3m42); don't skip it when deciding on a promotion.
- Gen3 compiles test programs correctly: `tools/saffron run test/hello_bootstrap.sf`

Stage 2 also **asserts that codegen's `Int` inference fallback count is 0** on the
compiler's own source, so `codegen fell back to `Int` N time(s)` is a bootstrap
failure with no compile error attached — the source is fine, inference gave up
somewhere it used to answer. The failure prints each site; the full log is
`build/stage4/unresolved.log`. Measure any file the same way with
`saffronc --report-unresolved`, or `SAFFRONC_FLAGS=--report-unresolved` through
`tools/saffron`. See stage 1 in `docs/design/compiler-rewrite.md`.

Stage 2 was added on 2026-07-31. Before that this section claimed the test stage
verified gen3 could compile itself, and it did not — it compiled
`test/hello_bootstrap.sf`, five lines of `IO.println`. A gen3 that rejected the
compiler's own source gave a fully green bootstrap, which is how ~100 latent
non-exhaustive matches (BUGS #76) stayed invisible.

**Ceremony:**

```bash
./bootstrap.sh                          # verify current bootstrap passes
tools/saffron run test/hello_bootstrap.sf  # verify gen3 output runs
cp build/saffronc build/stage2/saffronc    # promote
./bootstrap.sh                          # verify promoted gen2 still bootstraps
git add build/stage2/saffronc
git commit -m "Promote gen2: <new capability enabled>"
```

### Known gen2 pitfalls

The previous gen2 had several NaN-boxing and struct-layout bugs (arithmetic in list indices, pos-1 crashes, method addition causing linker errors). These are all **fixed** in the current promoted gen2 (June 2026). The compiler source now freely uses `this.tokens[this.pos + 1]`, `this.pos - 1`, 100+ methods on Parser, `parse_block_stmts()` as a helper, and `peek_is(...)` without issue.

**Current gen2 limitations** (constraints on what you can write in compiler source):

| Issue | Symptom | Workaround |
|-------|---------|------------|
| No tuple literal syntax in compiler source | `var t = (1, 2, 3)` parse error — gen2 has the `TupleLit` enum variant but can't parse tuple creation syntax | Use lists or multiple variables; tuple syntax works in user programs compiled by gen3 |

### Bootstrap file layout

```
build/
├── stage2/saffronc    ← gen2 (checked in, used to compile source)
├── saffronc           ← gen3 (built by bootstrap, the "current" compiler)
├── stage3/            ← intermediate .ll files from compilation
└── stage4/            ← gen4: gen3's own output, the fixed-point check (not
                         checked in, not used for anything but verification)
src/compiler/
├── codegen.sf         ← the Codegen class's shell up to line 557, PLUS live
│                        top-level free functions after it
├── codegen/*_body.sf  ← the class body (sed-assembled into codegen.sf)
├── parser.sf          ← parser (compiled directly)
├── lexer.sf           ← lexer (compiled directly)
└── main.sf            ← entry point with import resolution
```

The bootstrap `sed`-assembles each `codegen/*_body.sf` into the class at its
`@codegen-split:` marker and copies the rest of `codegen.sf` through unchanged.
Both halves are compiled, so a sweep over AST node shapes must cover
`codegen.sf` as well as the `*_body.sf` files.

## Known Issues

`BUGS.md` is the list. The count and open set are **derived, not stored**: run
`tools/bugs.sh` to print them (`--check` also flags any FIXED-titled entry
stranded in `## Open`). It reads the section an entry physically sits in — OPEN
iff its `### N.` heading is above the first `## Resolved` — which is the one
fact a merge cannot silently corrupt. `## Resolved` keeps closed entries in the
file for their narratives, so the file is long by design; do not try to eyeball
the count from its length.

The header still carries a hand-written `**N open entries: #...**` tally for
now. It is the single largest source of merge conflicts in this repo — every
branch that opens or closes an entry rewrites that one line, and concurrent
worktrees then collide on it — so trust `tools/bugs.sh` over it when they
disagree, and expect the stored line to be retired once the in-flight worktree
headers converge.

The one long-standing design limitation:
- **#2**: forward references in nested closures — compile-time local resolution,
  same as Lua/Python

#6 (`break`/`continue` outside a loop) and #122 (*assigning* to a nonexistent
module member) were both described here as open and are both fixed — `break`
outside a loop is now a checker error, and `Module.nope = 3` reports
`no member 'nope' in module 'Iter'` and exits 1. This section had them wrong for a
while, which is the argument for the paragraph above: read the state from
`BUGS.md`'s `## Open`, and when a claim about the compiler matters, probe the
binary. A four-line repro settles in seconds what a summary can be stale about for
weeks — and prose is staler than a count, because nothing forces it to move when
an entry closes.

#118 and #119 are fixed too, but the habit they should leave behind is worth
keeping: a compiler that exits 0 is not evidence it read your file. #119 in
particular made `saffronc in.sf out.sf.ll` compile the *output* path and exit 0,
so ad-hoc sweeps reported nothing while appearing to report everything. When you
measure, verify the thing you think you measured actually happened.
