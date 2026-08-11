# Saffron

Saffron is a statically typed scripting language with a **self-hosted compiler
written in Saffron**. It compiles source to LLVM IR via an AST intermediate
representation, then links to a native binary or a wasm32 module. The runtime
provides a garbage collector, NaN-boxed values, and cooperative async built on
LLVM coroutines.

## Features

- **Static typing with inference** — annotate where you want to (`var x: Int = 5`) or let types be inferred (`var name = "saffron"`). A dedicated type-checker pass runs before codegen.
- **Int / Float separation** — distinct `Int` and `Float` numeric types.
- **Enums and pattern matching** — algebraic enums with payloads, matched with `match`, including `is`-pattern matching over classes.
- **Classes, interfaces, and inheritance** — single-state inheritance plus interface conformance with abstract and default methods; multiple interface bases.
- **Closures and lambdas** — first-class functions with capture and `fun (x) => x * 2` arrow syntax.
- **String interpolation** — `"hello ${name}!"`.
- **Cooperative async and actors** — `Task.spawn`, `Async.await`, `Async.sleep`, and serialized-state `actor`s, built on LLVM coroutines.
- **Modules and imports** — builtin C modules, stdlib `@`-modules (`src/lib/*.sf`), and relative-path imports; named imports (`import { map } from "@iter"`).
- **Generics** — generic functions and generic enums (`enum Result<T, E> { Ok(value: T), Err(error: E) }`).
- **Operator overloading** — classes define `add`, `sub`, `mul`, `lt`, `eq`, etc.
- **First-class types** — `42 is Int`, `"hi" is String`; primitives carry methods (`(-5).abs()`, `(3.7).floor()`).
- **Collections** — `List` and `Map` literals with methods and index-based `for-in`.
- **Exception handling** — `try` / `catch` / `finally` catches both `throw`n values and runtime faults (index/division/null errors); an uncaught fault is fatal.
- **Compile targets** — native binaries and wasm32.

## Build

Saffron bootstraps from a checked-in compiler. The `gen2` binary at
`build/stage2/saffronc` compiles the current source into `gen3`:

```bash
./bootstrap.sh
```

The resulting native compiler is `build/saffronc`. There is no separate rebuild
from scratch — the checked-in `gen2` is the sole root of trust for the bootstrap
chain.

## Run

Use the `tools/saffron` driver for everyday work:

```bash
build/saffronc input.sf output.ll                          # compile to LLVM IR
tools/saffron run program.sf                               # compile + link + run
tools/saffron build program.sf -o app                      # native binary
tools/saffron build program.sf --target wasm32 -o app.wasm # wasm32 module
tools/saffron emit-ir program.sf                           # print LLVM IR to stdout
```

There is no REPL; use `tools/saffron run` for quick iteration. wasm32 linking
requires Homebrew LLVM (`/opt/homebrew/opt/llvm/bin/clang` +
`/opt/homebrew/bin/wasm-ld`); Apple's system clang cannot target wasm32.

## Examples

```saffron
enum Shape {
    Circle(radius: Int),
    Rect(width: Int, height: Int)
}

fun area(s: Shape): Int {
    return match (s) {
        Circle(r) => 3 * r * r
        Rect(w, h) => w * h
    }
}

IO.println(area(Shape.Circle(5)))   // 75
IO.println(area(Shape.Rect(3, 4)))  // 12
```

```saffron
import "@async" as Async

fun worker(name: String, duration: Float): String {
    Async.sleep(duration)
    return "${name} done"
}

var task = Task.spawn(fun () => worker("A", 0.1))
IO.println(Async.await(task))
```

## Architecture

The pipeline is:

**Lexer → Parser → Type Checker → LLVM IR Codegen → clang / wasm-ld**

- **Lexer** (`src/compiler/lexer.sf`) — tokenizes source into tokens.
- **AST** (`src/compiler/ast.sf`) — node definitions as enums.
- **Parser** (`src/compiler/parser.sf`) — builds the AST; also desugars `for-in`.
- **Type Checker** (`src/compiler/checker.sf`) — validates types before codegen.
- **Codegen** (`src/compiler/codegen/`) — emits LLVM IR, split across `expr`, `stmts`, `methods`, `match`, `closures`, `types`, `intrinsics`, `output`, `utils`.
- **Runtime** (`src/runtime/`) — the Saffron-level runtime plus LLVM IR bases for the native and wasm targets; the GC and NaN-box tagging live here.
- **Standard library** (`src/lib/`) — importable `.sf` modules.

Values are NaN-boxed `i64`s; doubles are stored unboxed. The compiler
self-hosts through a bootstrap chain (gen2 → gen3 → gen4 fixed point) — see
`CLAUDE.md` for the full model.

## Documentation

- `CLAUDE.md` — the authoritative, current guide to the build, architecture, language features, and bootstrap model.
- `docs/` — design notes, including `docs/design/compiler-rewrite.md`.
- `.github/contributing.md` — the contributor workflow.
- `BUGS.md` — open bugs (run `tools/bugs.sh` for the derived count and set); `BUGS_CLOSED.md` is the resolved archive.

## Tests

```bash
tools/run_tests.sh           # all suites
tools/run_tests.sh pass      # test/pass/*.sf — must compile AND run
tools/run_tests.sh fail      # test/fail/*.sf — the compiler MUST reject these
tools/run_tests.sh --network # also run network-dependent tests
```

## A note on `legacy/`

Saffron was originally implemented as a stack-based bytecode VM written in C.
That implementation now lives in `legacy/` and is **dead and unsupported** — it
has drifted far enough behind the language that it can no longer parse the
compiler's own source. Do not use it. The live implementation is the
self-hosted compiler described above.
