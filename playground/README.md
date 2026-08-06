# Saffron Playground

An in-browser Saffron playground: type a program, press Run, see its output. The
UI is itself written in Saffron — using the [Turmeric](../turmeric) reactive
framework, compiled to `wasm32` — so the playground is also a dogfooding exercise
for the framework and the wasm backend.

## Quick start

```bash
# from playground/

# 1. build the frontend (Turmeric -> wasm32)
cd frontend && .pantry/bin/turmeric-build && cd ..

# 2. publish it to where the service serves from
cp frontend/build/app.wasm frontend/build/app.js frontend/build/index.html \
   frontend/build/style.css static/

# 3. start the compile service
../tools/saffron run src/main.sf
```

Give step 3 about 15 seconds: it compiles and links the service before listening,
and a redirected stdout is block-buffered, so the banner appears late.

Then open **<http://127.0.0.1:8080>**.

**This now works end to end**: the page loads, the editor is populated, the
example buttons load programs, and Run compiles and executes them with output in
the right-hand pane. All seven bundled examples were driven through the real
shipped loader and produce correct output, as does the compile-error path.

Getting there closed the three defects this file used to list as blocking:

- `/app.wasm` served as 0 bytes (`BUGS.md` #66) — the UI module is now served
  base64-encoded from `GET /api/app_wasm`, because a Saffron `String` cannot hold
  a wasm binary at all (its magic number is `\0asm`, and the first byte terminates
  the string). Verified byte-identical end to end.
- The Run button never reaching the service — two separate bugs stacked: a `main`
  symbol collision (`BUGS.md` #71) and then a callback registry that could never
  find its own entries (`BUGS.md` #75).
- Death after a few dozen requests (`BUGS.md` #63) — `__gc_disable()` is now
  applied at startup, with the trade-off documented at the call site and below.

See "Known issues" for what remains.

The compile service can also be driven directly:

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
# {"ok":true,"wasm_toolchain":"ok","compiler_rev":"bc70635"}
```

`compiler_rev` is the git revision of the repo whose compiler the service invokes
at request time. **It must match `git rev-parse --short HEAD`.** If it differs (or
carries a `-dirty` suffix from an uncommitted tree), the service is compiling with
a compiler older than the current source — the staleness that makes an
already-fixed bug look unfixed. Because the service always launches via
`saffron run src/main.sf` (which recompiles from source), this normally matches;
a mismatch means you are running an old cached service binary or an unrebuilt
`build/saffronc`. **Never launch the service from a checked-in `./build/playground`**
— that binary is frozen at whatever compiler built it and is git-ignored for this
reason. Always use `saffron run src/main.sf`.

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
| `frontend/src/api.sf` | `fetch` wrapper, callback registry, minimal JSON |
| `frontend/public/app.js` | wasm loader glue: host imports, output capture, `__sched_pump` |
| `static/` | What the service serves. Copied from `frontend/build/` |
| `examples/*.sf` | The bundled example programs |

`frontend/public/app.js` is the **source of truth** for the loader;
`turmeric-build` copies it into `frontend/build/`, and from there it must be copied
to `static/`. Editing `static/app.js` directly works until the next build silently
reverts it.

```bash
cd frontend && .pantry/bin/turmeric-build && cd ..
cp frontend/build/app.wasm frontend/build/app.js frontend/build/index.html \
   frontend/build/style.css static/
```

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
- `GET /api/app_wasm` — the UI module itself, base64-encoded (see #66 below)
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
  server dies on a body well under the limit (`BUGS.md` #64).
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
- **The service's own heap is unbounded**, because the garbage collector is
  disabled to work around `BUGS.md` #63 (see "Known issues"). Memory grows for the
  life of the process, so an attacker does not need to find an allocation bug in
  the compiler — ordinary sustained traffic is enough.
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
`docs/design/playground-bug-log.md`, and the ones that still reproduce are filed
in `BUGS.md`. What still affects the playground, worst first:

- **The garbage collector is switched off** (`BUGS.md` #63). `@http/server`
  handles each connection in a spawned task, so its allocations live in a
  coroutine frame; the GC's shadow stack roots locals by the address of a stack
  `alloca`, which is not where a coroutine's locals live after a suspend. The
  collector frees live objects and the process exits silently — the last log line
  is still `Listening on ...`, which makes it look like a network fault rather
  than a crash. Measured here: dead after **23** requests to `/style.css`, or 60
  to `/api/health`. `main()` therefore calls `__gc_disable()`, which takes it to
  400+ requests clean. **The trade is an unbounded heap** — nothing is ever
  reclaimed, so a long-lived instance grows without limit. Fine for a local
  playground you restart freely; another reason this must not face untrusted
  traffic as it stands. Affects every Saffron HTTP server, not just this one.
- **A request body over ~35 KB kills the server outright** (`BUGS.md` #64).
  `@http/server` reads with a single `conn.read(8192)` and never checks for a
  short read, so a large body is parsed truncated and the process exits silently.
  This is also why the playground's own 64 KB source cap can never fire.
- **`static_files` still cannot serve a binary** (`BUGS.md` #66). Routed around
  rather than fixed: the UI module goes out base64-encoded via `/api/app_wasm`.
  The general defect stands — `Response.body` is `String`-typed and
  `Content-Length` comes from `.length()`, so serving binary assets needs a
  byte-length-carrying body type threaded through `Response`. Any other Saffron
  program serving a binary asset will hit this.
- **The frontend build needs an absolute `--lib-path`** (`BUGS.md` #57). A relative one
  makes the Turmeric prelude compile twice (`redefinition of global
  '@__g_turmeric_prelude__tc_event'`) because the compiler's module-dedup map is
  keyed on the path string. Worked around in `turmeric/tools/build.sf`.
- `fmod` and `js_time_now` are missing from the shared
  `turmeric/runtime/app_template.js` and are patched locally in
  `frontend/public/app.js` (log Bug 15).
- The frontend build logs `[checker] Warning: cannot infer type` and
  `[codegen] Warning: dispatching '<method>' on untyped value` in quantity. They
  are noise here, but they are also how a silently-dropped call presents, so they
  are worth reading rather than filtering.

### Fixed while getting the Run button working

Three bugs stacked up behind one symptom — a Run button that did nothing at all,
with no error anywhere. Each was invisible until the one in front of it was fixed:

1. **A `main` symbol collision** (`BUGS.md` #71). Codegen renamed *every* function
   named `main` to `__saffron_main` regardless of module, so the frontend's `main`
   collided with Turmeric's `<main>` element builder (`turmeric/src/prelude.sf`).
   The entry point silently called the *library's* function with the wrong arity,
   the user's `main` became unreachable, and `-O2` then legitimately stripped
   everything only it reached — including `js_fetch_post` and `js_run_wasm` (7
   imports instead of 16). The missing imports were the symptom; the original
   diagnosis in this file (an export-list ordering problem) was wrong. Fixed in
   `output_body.sf` and `expr_body.sf`; the entry point here is now `start()`,
   deliberately not `main`, since this file imports `main` from the prelude.
2. **A callback registry that could never find its own entries** (`BUGS.md` #75).
   `api.sf` kept pending callbacks in a `Map<Float, ...>` keyed by an id handed out
   to JS. A value that re-enters wasm from JS is a *bare machine integer*, but the
   id was stored as the f64 bit pattern of the same number, and map keys are
   compared by exact bit pattern — so every lookup missed and every completion
   handler was silently dropped. Now a `List` index, which only has to compare
   numerically. This is why Turmeric's own `__dispatch_event` is List-based.
3. **`json_field` blew the wasm heap on an 11 KB field.** It accumulated the value
   one character at a time, and Saffron strings are immutable, so each `+`
   reallocated everything so far — ~60 MB of garbage for the base64 `wasm` field,
   dying with `memory access out of bounds` inside `strcpy`. Now it scans for the
   closing quote and takes one `slice`, expanding escapes only for fields that
   actually contain a backslash.

Also fixed, and affecting every Turmeric app rather than just this one: the
bundler rewrote each app's `index.html` into N copies of its first line, because
`turmeric/tools/build.sf` used `var li: Float` as a list index and a `Float`-typed
index silently reads element 0 (`BUGS.md` #52). It presented as
"`StringBuilder.append` repeats its first argument". Two real wasm32
pointer-width bugs in `frontend/public/app.js` were also fixed — on wasm32
`malloc` takes an i64 but returns a 32-bit pointer, and handing that plain JS
Number to an import declared `-> i64` throws before the app can mount.

### Fixed upstream, before this work

- **Route dispatch** (`BUGS.md` #49) — `@http/server` used `var i: Number = 0` as a
  list index, which evaluated to `nan` and read the wrong route. Resolved by
  retiring the `Number` spelling from the stdlib (`485830a` and friends) rather
  than by changing what `Number` maps to. All routes now dispatch to the right
  handler.

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
