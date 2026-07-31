# Saffron Playground

An in-browser Saffron playground: type a program, press Run, see its output. The
UI is itself written in Saffron — using the [Turmeric](../turmeric) reactive
framework, compiled to `wasm32` — so the playground is also a dogfooding exercise
for the framework and the wasm backend.

## Quick start

```bash
# 1. build the frontend (Turmeric -> wasm32), from playground/frontend
cd frontend && .pantry/bin/turmeric-build && cd ..

# 2. start the compile service, from playground/
../tools/saffron run src/main.sf
```

Then open **<http://127.0.0.1:8080>**.

Three known defects currently limit this (all in "Known issues" below, with
detail). The blocking one: **the UI does not load in a browser at all**, because
`/app.wasm` is served as 0 bytes (`BUGS.md` #66). Beyond that, **the server dies
after roughly 85 requests** because of a garbage collector bug, so expect to
restart it. The Run button's dead `js_fetch_post` / `js_run_wasm` imports are
**fixed** (#71) — that was a `main` symbol collision, not the export-list problem
this file used to claim. Routing works too; the earlier 404s on `/` and
`/api/examples` were fixed upstream by retiring the `Number` type annotation.

The **compile service works end-to-end** and can be driven directly:

```bash
curl -s -X POST -H 'Content-Type: application/json' \
  -d '{"source":"IO.println(\"hi\")"}' http://127.0.0.1:8080/api/compile
# {"ok":true,"wasm":"AGFzbQ...","diagnostics":""}
```

Use `127.0.0.1`, not `localhost`. The Saffron server binds IPv4 only, and
`localhost` resolves IPv6-first on macOS, so `http://localhost:8080` can fail to
connect against a server that is running perfectly well.

Environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `PLAYGROUND_PORT` | `8080` | Port to listen on |
| `SAFFRON_ROOT` | inferred from `pwd` | Repo root, used to locate `tools/saffron` |

Check it came up correctly:

```bash
curl -s http://127.0.0.1:8080/api/health
# {"ok":true,"wasm_toolchain":"ok"}
```

If `wasm_toolchain` is anything other than `ok`, compilation will fail for every
request. It requires **Homebrew LLVM** — Apple's system clang cannot target
wasm32 at all:

```bash
brew install llvm            # provides /opt/homebrew/opt/llvm/bin/clang
brew install binaryen lld    # provides /opt/homebrew/bin/wasm-ld
```

`/api/health` also warns if `timeout(1)` is missing (`brew install coreutils`),
because without it compiles are unbounded.

## How it works

The browser never runs the Saffron compiler; it runs only *compiled output*.

```
browser                          host (this service)
───────                          ───────────────────
editor (Turmeric/wasm32)
   │  POST /api/compile
   │  {"source": "..."}    ──▶   mkdir /tmp/saffron_playground_<token>
   │                             write main.sf
   │                             timeout 20 tools/saffron build --target wasm32
   │                             base64 the .wasm;  rm -rf the temp dir
   │  ◀── {"ok":true,"wasm":"<base64>","diagnostics":"..."}
   │
   └─ WebAssembly.instantiate(...) in a *second*, isolated instance
      captured stdout ──▶ output pane
```

The user's module is instantiated as a separate `WebAssembly.Instance` with its
own memory, distinct from the UI's instance. The only channel between them is the
captured output string, so a user program cannot reach into the UI's heap.

### Layout

| Path | What it is |
|---|---|
| `src/main.sf` | HTTP service: routes, JSON, static file serving |
| `src/compile.sf` | The compile sandbox: temp dir, timeout, size cap, base64 |
| `src/examples.sf` | Serves the bundled examples as JSON |
| `frontend/src/main.sf` | The UI, in Turmeric (editor, run button, output pane) |
| `frontend/src/api.sf` | `fetch` wrapper for `/api/compile` |
| `frontend/public/app.js` | wasm loader glue: host imports, output capture, `__sched_pump` |
| `static/` | Build output served to the browser (`app.wasm` + copied assets) |
| `examples/*.sf` | The bundled example programs |

The seven examples are `hello` (strings, interpolation, inference), `fizzbuzz`
(loops, conditionals), `collections` (lists, maps, destructuring, named imports),
`closures` (lambdas, higher-order functions), `enums_match` (enums, pattern
matching, generic `Option`), `classes` (inheritance, interfaces, operator
overloading) and `async` (`Task.spawn`, `await`, the browser-driven scheduler).
All seven are verified to compile to wasm32 through the service and to produce
correct output.

### API

- `GET /` — the frontend
- `GET /api/health` — liveness + whether the wasm toolchain is usable
- `GET /api/examples` — the bundled examples
- `POST /api/compile` — `{"source": "..."}` → `{"ok", "wasm", "diagnostics"}`

## Sandboxing: what is and is not contained

Read this before exposing the service to anyone you do not trust.

### What is contained

**User code does not execute on the host.** This is the main safety property. The
host compiles; the browser runs. A submitted program's `system`/`popen`-reachable
behaviour never gets a host process — it only ever runs inside the browser's wasm
sandbox, limited to the imports `frontend/public/app.js` chooses to provide (no
filesystem, no network, no process spawning).

Also in place:

- **Per-request temp directory** (`/tmp/saffron_playground_<clock>_<counter>`),
  deleted when the request finishes. Concurrent compiles cannot collide, and one
  request's artifacts are never visible to the next.
- **Wall-clock timeout** on the build (`COMPILE_TIMEOUT_SECS = 20`, via
  `timeout(1)`), so a compiler hang cannot wedge a worker.
- **Source size cap** (`MAX_SOURCE_BYTES = 64000`), checked before a process is
  spawned — but see the note below: it is currently **unreachable**, because the
  server dies on a body well under the limit.
- **`rm -rf` target guard** — cleanup refuses any path not beginning with
  `/tmp/saffron_playground_`.
- **Host paths stripped from diagnostics**, so temp directory names are not
  disclosed.
- **Output caps in the loader** (`MAX_OUTPUT_CHARS`, `MAX_PUMPS`) so a runaway
  program cannot hang the browser tab.

### What is NOT contained

- **The compiler itself is not sandboxed.** It runs as your user, with your
  permissions, on attacker-supplied input. A memory-safety bug or a code-execution
  bug in the Saffron compiler is directly reachable from any submitted program.
  This is the single most significant residual risk — and it is not hypothetical,
  since the compiler is under active development and does segfault on some inputs.
- **The source size cap does not actually work** (`BUGS.md` #64). `@http/server`
  reads the request with a single `conn.read(8192)` and never checks for a short
  read, so a body over roughly 35 KB hands the parser a truncated request and
  silently kills the process. The application-level 64000-byte check never gets
  to run. A size limit has to be enforced by the server while reading; until then,
  a single 40 KB POST is an unauthenticated denial-of-service.
- **No CPU, memory, or disk limits.** There is a wall-clock timeout, but no
  `rlimit`. A program that makes the compiler allocate without bound can exhaust
  host memory well inside 20 seconds.
- **No rate limiting and no authentication.** Every request spawns a compiler
  process; a trivial loop is a denial-of-service.
- **No network egress restrictions** on the compiler process.
- **Binds `0.0.0.0`**, so it is reachable from your whole network, not just
  loopback.
- **The temp directory is world-traversable `/tmp`**, with no per-request user
  separation.

### If you want to expose this publicly

At minimum: run the build inside a container or VM with a read-only root and no
network; add `rlimit` caps on CPU, address space, and file size; add rate limiting
and a concurrency cap; bind to loopback behind a reverse proxy. Treat every
submitted program as hostile input to the compiler, because that is exactly what
it is.

## Known issues

A running log of the Saffron and Turmeric bugs found while building this is at
`docs/design/playground-bug-log.md` (35 entries), and the ones that still
reproduce are filed in `BUGS.md`. The ones that affect the playground right
now, worst first:

- **`/app.wasm` is served as 0 bytes, so the UI never loads** (`BUGS.md` #66).
  `static_files` reads with `IO.read_file`, and a Saffron `String` is
  NUL-terminated — a wasm module's magic is `\0asm`, so the read stops on the very
  first byte and returns `""`. The response is a well-formed `200` with
  `Content-Length: 0`. Serving it properly needs a byte-length-carrying body
  through `Response`, not just a fixed read: `Response.body` is `String` and
  `Content-Length` comes from `.length()`. Until then the frontend can only be
  loaded by a server that does not go through `static_files`, and the compile
  service must be driven with `curl`.
- **A request body over ~35 KB kills the server outright** (`BUGS.md` #64).
  `@http/server` reads with a single `conn.read(8192)` and never checks for a
  short read, so a large body is parsed truncated and the process exits silently.
  This is also why the playground's own 64 KB source cap can never fire.
- **The server dies after roughly 85 requests** (`BUGS.md` #63). This is a garbage
  collector bug, not a playground bug, and it affects *every* Saffron HTTP
  server. `@http/server` handles each connection in a spawned task, so its
  allocations live in a coroutine frame; the GC's shadow stack roots locals by
  the address of a stack `alloca`, which is not where a coroutine's locals live
  after a suspend. The collector frees live objects and the process exits
  silently — the last log line is still `Listening on ...`, which makes it look
  like a network fault rather than a crash. Ten-line repro and the full analysis
  are in the bug log. `__gc_disable()` at startup makes it go away (399 requests
  clean instead of 84), but that trades the crash for an unbounded heap, so it is
  deliberately *not* applied here. **Just restart the server when it goes quiet.**
- ~~**The UI's Run button cannot reach the service**~~ — **fixed** (`BUGS.md` #71).
  The dropped `js_fetch_post` / `js_run_wasm` imports were a *symptom*, and the
  original diagnosis here (the wasm32 export list being applied after `-O2` had
  stripped callback-reachable externs) was wrong. The real cause: codegen renamed
  every function named `main` to `__saffron_main` regardless of module, so the
  frontend's `main` collided with Turmeric's `<main>` element builder
  (`turmeric/src/prelude.sf`). The entry point silently called the *library's*
  function with the wrong arity, the user's `main` became unreachable, and `-O2`
  then stripped everything only it reached — including those two imports (7
  instead of 16). With the rename scoped to the entry module, the linked module
  carries all 16 imports again.
- **The frontend build needs an absolute `--lib-path`** (`BUGS.md` #57). A relative one
  makes the Turmeric prelude compile twice (`redefinition of global
  '@__g_turmeric_prelude__tc_event'`) because the compiler's module-dedup map is
  keyed on the path string. Worked around in `turmeric/tools/build.sf`.
- `fmod` and `js_time_now` are missing from the shared
  `turmeric/runtime/app_template.js` and are patched locally in
  `frontend/public/app.js` (log Bug 15).

### Fixed upstream since this was written

- **Route dispatch** (`BUGS.md` #49) — `@http/server` used `var i: Number = 0` as a
  list index, which evaluated to `nan` and read the wrong route. Resolved by
  retiring the `Number` spelling from the stdlib (`485830a` and friends) rather
  than by changing what `Number` maps to. All six routes now *dispatch* to the
  right handler, re-verified this session — but `/app.wasm` dispatches correctly
  and then returns an empty body, see #66 above. Health, `/api/examples`, `/` and
  the 404 path return correct content.

### Compromises in the examples

The bundled examples are meant to read as idiomatic Saffron. Three places where
they do not, each because the natural spelling is currently broken:

- `examples/collections.sf` sums a map by walking `counts.keys()` and calling
  `.get(key)`, instead of `for (entry in counts)`. Map iteration is a documented
  feature that segfaults — `for-in` is desugared in the parser with no type
  information and always emits `length()` + `[i]`, so a Map is read as a List and
  the integer cursor is passed to `__map_get` as a *key* (`BUGS.md` #62). The same
  mechanism means the documented `.iter()`/`.has_next()`/`.next()` protocol is
  never used, so custom iterables do not work either.
- `examples/classes.sf` calls `rex.speak()` on the concrete type rather than
  through a `List<Animal>`, and its interface declares only the abstract
  `area()` with no default method. Both are the same bug (`BUGS.md` #50): an inherited
  method's `this.foo()` binds to the base implementation, so a default method
  built on an abstract one reads `0` and a `List<Base>` never reaches an
  override. Virtual dispatch is implemented on a separate branch; when it lands,
  both should revert to the natural form — it is the biggest readability win
  available here.
- `examples/fizzbuzz.sf` writes `for (i: Int = 1; ...)` because
  `for (var i = 1; ...)` is a parse error (`BUGS.md` #60) — the C-style `for` header is
  the one binding form that neither accepts `var` nor infers a type.

## Debugging tips

Two traps cost real time while building this; both are easy to hit again.

**Stale servers hold your port.** `tools/saffron` execs a temp binary
(`/tmp/saffron_build_*/program`), so `pkill -f 'saffron run src/main.sf'` does
*not* match it. Old servers survive, keep their ports, and answer your `curl` —
which looks exactly like a routing bug. Always confirm the listener is yours:

```bash
lsof -ti:8080                      # who actually holds the port
grep -E 'Listening|failed to bind' <your log>
```

A `failed to bind` in the log plus a successful `curl` means you are talking to
something else entirely.

**A backgrounded server looks like a hang.** `IO.println` to a redirected stdout
is block-buffered, so the startup banner may not appear for a long time. Give the
server ~15s before concluding it is stuck, and prefer a log file you can `tail`.
