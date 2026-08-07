# Build & Linking: Saffron vs. mature languages

Status: analysis, 2026-08-06. Written after BUGS #181 (coroutines fail to link
without `import "@async"`) exposed that Saffron has no notion of *ambient
runtime* — everything, including the scheduler, is a source module folded into
one translation unit. This document is the factual comparison that motivates a
proper linking model, and it feeds two open threads: #181 (link the scheduler as
runtime) and the compiler-rewrite stages 6–7 (structural LLVM emission).

## How Saffron builds today (measured, not assumed)

The whole pipeline, end to end:

1. `tools/saffron` invokes `saffronc <entry.sf> output.ll`.
2. Inside the compiler, `main.sf` **collects every transitively-imported module**
   (`collect_modules`, text-based `extract_imports` + `@`/relative/package
   resolution), parses each, and concatenates them into ONE program with a
   per-module name prefix (`stdlib_iter_`, `stdlib_scheduler_`, …). A `visited`
   set dedupes by resolved path.
3. Codegen emits ONE `output.ll` for the entire program — user code and every
   imported stdlib module in a single translation unit.
4. `clang -O2` links that one `output.ll` against a fixed, hand-written set of
   runtime `.ll` files, chosen by target:
   - native: `build/stage3/runtime.ll` (the Saffron-level runtime, itself
     compiled from `runtime.sf`) + `base_nanbox.ll` + `gc.ll`
   - wasm64: `wasm_base.ll` + runtime
   - wasm32: `wasm_base_32.ll` + runtime, plus a `--export`/`--allow-undefined`
     dance for JS-boundary symbols

**Properties that fall out of this model:**

- **Whole-program, single translation unit.** There is no separate compilation.
  Importing `@iter` recompiles `iter.sf` into your program every build. There is
  no `.o` cache, no incremental relink, no archive.
- **The "runtime" is four hand-maintained `.ll` files** plus one `.sf`
  (`runtime.sf`). They are the only things linked that were not produced by the
  current compile. `__val_class_tag`, `__list_push`, `__gc_alloc`, NaN-box
  tag/untag live here.
- **There is no ambient runtime library.** The scheduler, unlike the GC or the
  list primitives, is a *source module* (`@scheduler`) you must import to get.
  That is exactly the #181 defect: codegen emits `@stdlib_scheduler_*` calls
  whenever a coroutine exists, but the symbols only appear if some import pulled
  the scheduler source in.
- **Symbol names are the ABI.** Cross-module calls resolve by mangled name
  (`stdlib_iter_map`), decided at codegen time from the module prefix. There is no
  symbol table handed between compilation units, because there is only one unit.
- **Dependency resolution is a text scan.** `extract_imports` greps `import`
  lines; there is no manifest-driven resolver, lockfile, or version selection
  beyond `pantry.toml` `entry` lookup.

## Comparison

The axis that matters for #181 is **"what is ambient runtime vs. what is a
compiled dependency,"** and secondarily **"whole-program vs. separate
compilation."**

### Elixir / BEAM — the parity target for #181

- **Runtime is a pre-built VM.** BEAM exists independently of your code; you never
  "link" it and never opt into the scheduler — preemptive scheduling, the process
  model, and GC are properties of the machine your bytecode runs on.
- Compilation is per-module to `.beam` bytecode; the VM loads modules dynamically.
  There is no static link step at all.
- **Lesson for Saffron:** the scheduler should be like BEAM — always present,
  never imported. Saffron can't adopt a VM (it compiles to native/wasm), but it
  *can* treat the scheduler as ambient runtime: compile `scheduler.sf` once to
  `scheduler.ll` and always link it, exactly as `runtime.ll`/`gc.ll` are. That is
  the #181 plan.

### Go

- **Runtime is a library linked into every binary**, always — the goroutine
  scheduler, GC, and channels are in `runtime`, not opted into. This is the
  static-binary analogue of BEAM and the closest match to what Saffron should do.
- Separate compilation to `.o` per package, archived (`.a`), then a single static
  link. Fast because of aggressive caching (`$GOCACHE`) and because the linker
  only pulls referenced archive members.
- **Lesson:** "always link the runtime, including the scheduler" is a solved,
  mainstream design — Go proves it doesn't cost you unless you use it, because the
  linker drops unreferenced functions. Saffron's whole-program `-O2` gets the
  dead-code elimination for free; what it lacks is the *always-linked* part for
  the scheduler.

### Rust

- **Runtime is minimal and static** (`libcore`/`liballoc`/`libstd`), linked into
  every binary. Crucially, **async has NO built-in runtime** — you pick tokio /
  async-std as a normal dependency. This is the opposite choice from Go/BEAM.
- Separate compilation per crate (the compilation unit is the crate, not the
  file), `.rlib` archives, incremental compilation caches, and a real resolver
  (Cargo + `Cargo.lock`) with semver version selection.
- **Lesson / the fork in the road:** Rust says "async runtime is a library you
  choose"; Go/BEAM say "the scheduler is ambient." Saffron's codegen *already
  emits scheduler calls unconditionally* for any coroutine, so it has effectively
  chosen the Go/BEAM model in codegen — #181 is just making the *link* agree with
  what codegen already assumes. Adopting Rust's model would instead mean codegen
  must not emit scheduler calls unless a runtime is imported, which is a larger
  change and contradicts the current design.

### Swift

- Runtime + standard library are dynamically linked (`libswiftCore`); the
  concurrency runtime (the cooperative thread pool behind `async`/`await`) ships
  as part of the runtime — again ambient, like Go.
- Separate compilation, module `.swiftmodule` interface files, a real driver
  coordinating frontend jobs.
- **Lesson:** another mainstream language where structured concurrency's scheduler
  is ambient runtime, not an imported library. Reinforces the #181 direction.

### OCaml / C

- OCaml: separate compilation (`.cmo`/`.cmx` + `.cmi` interface files), a runtime
  linked into every binary, no built-in scheduler (threads/lwt are libraries).
- C: the model Saffron's link step literally is — hand the linker a set of objects
  and let it resolve symbols. libc is the ambient runtime; everything else is
  explicit. Saffron is closest to C here, minus separate compilation.

## Where Saffron sits, and what it implies

| Dimension | Saffron today | Mainstream norm |
|---|---|---|
| Compilation unit | whole program (one `.ll`) | per-module/crate/package |
| Incremental build | none — recompiles all imports every time | cached `.o`/`.rlib`/`.beam` |
| Runtime | 4 hand-written `.ll` + `runtime.sf`, always linked | a runtime library, always linked |
| Scheduler | **source module, opt-in via import** | **ambient runtime (Go/Swift/BEAM)** |
| Cross-module ABI | mangled names in one unit | symbol tables across units |
| Dependency resolution | text-scan `import` + `pantry.toml` entry | manifest + lockfile + version solver |

**The #181 conclusion, in this frame:** every mainstream language that has a
built-in cooperative/green-thread scheduler (Go, Swift, BEAM) makes it *ambient
runtime* — always linked, never imported. Saffron's codegen already behaves as if
that were true (it emits scheduler calls for any coroutine). The bug is purely
that the *link* still treats the scheduler as an opt-in source module. The fix is
to move `scheduler.ll` into the always-linked runtime set alongside `runtime.ll`
and `gc.ll`, and stop collecting `@scheduler` as a module — matching what four
other production languages do.

**The larger, non-#181 observations (for later):**

- **No separate compilation is the biggest scaling gap.** Recompiling all of
  stdlib into every program is fine at the current tree size but is O(program +
  all-transitive-stdlib) per build. Real projects with deep dependency trees will
  feel this. A `.o`/archive cache keyed by module + version is the standard
  answer; it also requires a stable cross-unit ABI (today the ABI is "one unit, so
  no ABI"), which is a prerequisite, not a detail.
- **The runtime bases are hand-maintained per target** (`base.ll`,
  `base_nanbox.ll`, `wasm_base.ll`, `wasm_base_32.ll`) — the exact duplication
  compiler-rewrite invariant I10 (one source of truth) and stage 9 (generated
  runtime, one header → four targets) target. #181's scheduler-as-runtime work
  should land in a way that doesn't add a fifth hand-maintained artifact per
  target — ideally `scheduler.ll` is generated by compiling `scheduler.sf` in the
  bootstrap, not hand-written.

## Concrete next step (when #181 is picked up)

1. `bootstrap.sh`: compile `src/lib/scheduler.sf` → `build/stage3/scheduler.ll`
   (same shape as the `runtime.sf` → `runtime.ll` step already there).
2. `tools/saffron`: add `scheduler.ll` to all three target link lines
   (native, wasm64, wasm32), beside `runtime.ll`.
3. `main.sf`: exclude `@scheduler` from `collect_modules` — its symbols now come
   from the linked `.ll`, so collecting the source would double-define them.
4. Re-verify the 9 current `@scheduler` importers (`async.sf`, `net.sf`, and 7
   tests) and the bootstrap's own build (which must not regain the
   reflect-emission coupling described in #181).
