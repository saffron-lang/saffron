# `@thread` Module — Implementation Plan

**Status:** Plan
**Date:** 2026-08-06
**Supersedes:** the "Implementation Plan" half of `docs/design/threading.md`
(2026-06-03), which surveyed model options. This document commits to one path
and sequences it into shippable workstreams.
**Depends on:** native (LLVM) target only; cooperative async (`src/lib/scheduler.sf`),
the `@extern`/`sf_*` C-helper pattern, and the existing `Sendable` checker
support (`src/compiler/checker.sf:836`).

---

## 0. The one decision that shapes everything: the GC is not thread-safe

The garbage collector is a single set of process-global LLVM globals, with no
locking anywhere:

- `@__gc_head`, `@__gc_alloc_count`, `@__gc_total_bytes`, `@__gc_threshold`,
  `@__gc_shadow_stack`, `@__gc_shadow_stack_inited` are all `global i64`
  (`src/runtime/gc.ll:30-38`). There is exactly one shadow stack for the whole
  process (`@__gc_init_shadow_stack`, `gc.ll:142`), and one allocation linked
  list.
- `@__gc_alloc` (`gc.ll:401`) walks and mutates that list, bumps
  `@__gc_total_bytes`, and *triggers `@__gc_collect` inline* when the threshold
  is crossed. `@__gc_collect` (`gc.ll:1197`) does a global mark (walking the one
  shadow stack + globals) and sweep of the one linked list.

If two OS threads call `@__gc_alloc` concurrently, the results are: torn writes
to the free-list head and byte counters; a sweep on thread A freeing objects
thread B is mid-allocating; a mark that walks a shadow stack thread B is
simultaneously pushing/popping. This is immediate heap corruption and
use-after-free — not a rare race, a routine one, because *every* allocation
touches this state.

The NaN-box runtime tagging (`__rt_tag_ptr`, `src/runtime/runtime.sf`) is pure
and reentrant, so tagging itself is fine; the hazard is exclusively the
allocator/collector and the shadow stack.

**Consequence for the plan:** there are only two honest ways to ship threads
without rewriting the GC, and we do them in order:

1. **v1 — Global Runtime Lock (GRL, a GIL).** Only one thread runs *managed*
   Saffron code (i.e. code that may allocate or touch the heap) at a time. The
   lock is released only around explicitly-blocking native calls. This requires
   **zero GC changes and zero codegen changes** and is completely safe. It buys
   **I/O and blocking-FFI parallelism** (the "a synchronous C call freezes the
   whole scheduler" problem in `threading.md` §1), but *not* CPU parallelism for
   pure-Saffron compute. This is exactly CPython's model and is defensible and
   shippable.

2. **v2 — True parallelism via stop-the-world + per-thread shadow stacks +
   allocator lock.** This unlocks multi-core CPU-bound Saffron. It is a real GC
   project (thread-local shadow stack, a global allocator mutex, and
   codegen-inserted safepoints so the collector can pause every mutator). It is
   staged last precisely because it is the expensive, risky part, and v1
   delivers value without it.

Leading with the GRL is what makes this incremental: everything in Workstreams
A–D below runs correctly under the GRL and does not have to be revisited when v2
lands.

---

## 1. Scope and non-scope

### In scope (v1, GRL-based)
- `Thread.spawn(fn) -> ThreadHandle`, `handle.join() -> Any`, `Thread.detach(fn)`.
- `Thread.Mutex` (real `pthread_mutex_t`), `Thread.sleep(seconds)` (OS sleep,
  not a scheduler yield).
- `Thread.Channel<T>(cap)` — a real cross-thread channel (mutex + condvar).
- `Thread.Atomic` (integer atomics).
- The async bridge: `Async.spawn_blocking(fn)` and `Async.await_thread(handle)`,
  built on the **self-pipe → fd → yield-reason-2** trick so the scheduler needs
  **no new yield reason** (see §5).

### Explicitly NOT in v1 (see §9)
CPU parallelism for pure-Saffron code, per-thread schedulers, thread pools with
work-stealing, structured concurrency/`scope`/nursery, `RWLock`, a `--race`
detector, and any wasm support.

---

## 2. Why v1 needs no compiler or bootstrap changes

This is the reason `@thread` can ship as "just another native stdlib module,"
like `@socket`/`@process`/`@watch`:

- **Closures are already a callable `[fn_ptr, env_ptr]` pair.** Codegen emits a
  16-byte heap pair — slot 0 is the function pointer, slot 1 is the captured
  environment (`src/compiler/codegen/closures_body.sf:146-171`). The function's
  calling convention is `i64 (i64, ...)` with the env passed as the first `i64`
  argument (`closures_body.sf:53-57`). A C thread-entry shim can therefore invoke
  a zero-arg Saffron closure by reading `pair[0]` as a function pointer and
  calling it with `pair[1]`. No new codegen is required to hand a closure to a
  thread.
- **`Sendable` already exists.** `Checker.is_sendable` (`checker.sf:836-853`)
  classifies Int/Float/Bool/String/Nil/Any, actors, and enum variants as
  sendable; List/Map/Fun as not. `Task.spawn` already warns on non-Sendable
  captures (`checker.sf:3039`, `codegen/closures_body.sf:14-23`). We reuse this
  verbatim for `Thread.spawn` — the value-sharing safety story is *already
  designed*, we just point it at a second spawn site (§4).
- **No new syntax** ⇒ no gen2/gen3 constraint. The module is `@extern`
  declarations + ordinary classes, so `build/stage2/saffronc` never needs to
  understand anything new, and the compiler source never imports `@thread`. The
  bootstrap chain (`CLAUDE.md` §"Bootstrap & New Syntax") is untouched.

The only build-system change is one edit to the native link line (§7).

---

## 3. Runtime: `src/runtime/thread_native.c` (new)

Modeled directly on `signal_native.c` (the recent self-pipe model) and
`process_native.c`'s handle-table style. All exported symbols are `sf_thread_*`
returning/taking untagged `int64_t` (CLAUDE.md: "All `extern` parameters must be
untagged").

### 3.1 The Global Runtime Lock

```c
static pthread_mutex_t sf_grl = PTHREAD_MUTEX_INITIALIZER;
void    sf_grl_lock(void)   { pthread_mutex_lock(&sf_grl); }
void    sf_grl_unlock(void) { pthread_mutex_unlock(&sf_grl); }
```

The main thread holds the GRL for its entire run (acquired once in the process
entry path — see §6). A spawned worker acquires it before running any Saffron
code and releases it only inside blocking primitives. The invariant is: *you may
not touch a heap object or allocate unless you hold the GRL.*

### 3.2 Thread entry shim

```c
typedef struct {
    int64_t closure;       // NaN-untagged ptr to [fn_ptr, env_ptr]
    int64_t result;        // filled on completion
    int64_t done;          // 0/1, published under sf_grl
    int done_pipe_w;       // self-pipe write end for the async bridge (or -1)
    pthread_t tid;
} sf_thread_t;

static void *sf_thread_trampoline(void *arg) {
    sf_thread_t *t = arg;
    int64_t *pair = (int64_t *)t->closure;      // [fn_ptr, env_ptr]
    int64_t (*fn)(int64_t) = (int64_t (*)(int64_t))pair[0];
    int64_t env = pair[1];

    sf_grl_lock();                               // no managed code before this
    int64_t r = fn(env);                         // runs Saffron closure body
    t->result = r;
    t->done = 1;
    sf_grl_unlock();

    if (t->done_pipe_w >= 0) {                   // wake an awaiting coroutine
        unsigned char b = 1; ssize_t w;
        do { w = write(t->done_pipe_w, &b, 1); } while (w < 0 && errno == EINTR);
    }
    return NULL;
}
```

Exports: `sf_thread_spawn(closure, want_pipe) -> handle`,
`sf_thread_join(handle) -> result`, `sf_thread_detach(handle)`,
`sf_thread_done_fd(handle) -> fd`, `sf_thread_sleep(seconds)` (releases the GRL
around `nanosleep`), and a small handle table like `process_native.c:62-101`.

**GRL discipline in `sf_thread_join`:** `join` must *release* the GRL before
`pthread_join`, or the joining thread deadlocks the worker (the worker can never
finish because it needs the GRL the joiner holds). Pattern:
`sf_grl_unlock(); pthread_join(tid, NULL); sf_grl_lock();`. Every blocking
primitive follows this "drop GRL, block, re-take GRL" shape — that is the whole
concurrency mechanism.

### 3.3 Mutex, Atomic, Channel

- `Thread.Mutex`: `sf_mutex_new/lock/unlock/free` over a heap `pthread_mutex_t`.
  `lock()` drops the GRL around `pthread_mutex_lock` (same reason as join).
- `Thread.Atomic`: thin wrappers over `__atomic_*` (`sf_atomic_load/store/
  add/sub/cas`). NaN-boxed ints are 48-bit and fit a 64-bit atomic word;
  operate on the untagged payload and re-tag Saffron-side.
- `Thread.Channel`: the `ThreadChannel` struct from `threading.md` §8.7 (ring
  buffer + `pthread_mutex_t` + two condvars + `closed`). `send`/`recv` drop the
  GRL while waiting on the condvar. Because a channel only ever carries Sendable
  values (enforced at the API boundary, §4), what crosses the channel is a
  small NaN-boxed scalar or an immutable String — no shared mutable object graph.

---

## 4. Stdlib: `src/lib/thread.sf` (new)

Extern block (untagged `Int` params), then thin classes — mirrors how
`concurrent_map.sf` wraps primitives.

```saffron
@extern("i64 sf_thread_spawn(i64, i64)") fun _spawn(closure: Int, want_pipe: Int): Int
@extern("i64 sf_thread_join(i64)")       fun _join(handle: Int): Any
@extern("i64 sf_thread_done_fd(i64)")    fun _done_fd(handle: Int): Int
@extern("void sf_thread_sleep(double)")  fun _sleep(seconds: Float)
// ... mutex / atomic / channel externs
```

`Thread.spawn` returns a GC-managed `ThreadHandle` wrapping the C handle so it
is not leaked. `handle.join()` returns `Any` (same reasoning as
`scheduler.sf:18-25` — the result is an opaque NaN-boxed value; annotating it
`Int` would re-tag and corrupt a Float/ptr result, BUGS #38).

**Value-sharing safety (the Sendable reuse).** Add `Thread.spawn` to the same
`check_lambda_sendable` path already used for `Task.spawn`
(`checker.sf:3039`, `codegen/closures_body.sf:14-23`). This is the single
compiler touch in v1 and it is additive: one more callee name recognized by the
existing check, emitting the existing warning when a thread closure captures a
List/Map/Fun. Documented rule (matching `threading.md` §4.6): immutable scalars
and Strings are safe to share; mutable containers must be passed through a
`Thread.Channel` or guarded by a `Thread.Mutex`.

**Relationship to actors.** Actors already provide *serialized state access*, and
`is_sendable` already treats actor references as Sendable (`checker.sf:841`). In
v1 actors remain bound to the main-thread scheduler — a worker thread must not
invoke actor methods, because actor dispatch suspends into the scheduler, which
only the main thread runs. The plan documents actors as the recommended *safe
shared-state* primitive but scopes cross-thread actor calls out of v1 (they need
the per-thread-scheduler work in §9).

---

## 5. The async bridge (no scheduler changes)

This is the elegant part and the reason to study `signal_native.c` first. The
scheduler already parks a coroutine on "a byte is readable on an fd" as
**yield reason 2** (IO-read): `scheduler.sf` pushes the handle onto
`io_handles`/`io_fds` with mode 0, and `_poll_io` revives it via `tcp_poll` when
the fd is ready.

So `spawn_blocking`/`await_thread` need **no new yield reason and no scheduler
edits**:

1. `sf_thread_spawn(closure, want_pipe=1)` creates the worker *and* a self-pipe;
   `sf_thread_done_fd` returns the read end.
2. The worker writes one byte to the pipe when it finishes (§3.2).
3. `Async.await_thread(handle)` does `__suspend(2, done_fd)` — parking the
   coroutine on that fd exactly like a socket read. Other coroutines keep running
   on the main thread.
4. When the worker finishes, the byte lands, `_poll_io` wakes the coroutine, and
   it calls `_join` (which now returns immediately) to collect the result.

`Async.spawn_blocking(fn)` is `await_thread(Thread.spawn(fn))`. This directly
solves the "blocking FFI freezes the scheduler" motivation: a synchronous C call
now runs on a worker while the event loop keeps turning. The read-end fd is set
non-blocking exactly as in `signal_native.c` so a spurious poll wake returns
`-1` instead of blocking the single-threaded runtime.

---

## 6. Process entry / GRL bootstrapping

The main thread must acquire the GRL before running `main`. The cleanest hook is
the runtime init already emitted around `@__gc_init_shadow_stack`. Add a
`sf_grl_lock()` call in the same startup path (a one-line addition to the entry
sequence in `base_nanbox.ll` / the codegen entry preamble in
`codegen/output_body.sf`). Because a single-threaded program simply takes an
uncontended mutex once, the cost is negligible and behavior is unchanged when
`@thread` is never imported.

---

## 7. Build driver

One edit to `tools/saffron`. The native branch defines and links the existing
`*_native.c` files:

```
local THREAD_NATIVE="$SCRIPT_DIR/src/runtime/thread_native.c"
# ... add "$THREAD_NATIVE" to the clang link line, and -lpthread
```

`-lpthread` on Linux (no-op/implicit on macOS). The wasm32/wasm64 branches are
left alone — `@thread` is host-only (§8).

---

## 8. wasm32/wasm64: host-only, like signal/socket

wasm32 has no threads in this toolchain (the link line uses
`--no-standard-libraries` and single-threaded SJLJ). `@thread` is therefore
host-only, matching `@socket`/`@process`/`@watch`. Two concrete guards:

- The C helpers are only in the native link line, so a wasm build that imports
  `@thread` fails at link with undefined `sf_thread_*` — acceptable but blunt.
- Better: `thread.sf` provides a wasm fallback where `Thread.spawn(fn)` runs the
  closure *synchronously* and returns an already-completed handle (the same
  degradation actors already take on wasm — actor methods compile as synchronous
  calls per CLAUDE.md §Actors). `Thread.sleep` maps to a busy/no-op, `Mutex`
  becomes a no-op. This keeps single-file source portable; document that wasm
  gives no parallelism.

---

## 9. What we deliberately do NOT do in v1

- **CPU parallelism for pure-Saffron code.** Under the GRL, two threads cannot
  run Saffron code simultaneously. This is the honest cost of not touching the GC
  yet. Deferred to v2.
- **Stop-the-world GC, per-thread shadow stacks, safepoints, TLABs.** The entire
  v2 GC workstream (§0): thread-local `@__gc_shadow_stack`, a `pthread_mutex`
  around `@__gc_alloc`/`@__gc_collect`, a thread registry, and codegen-inserted
  safepoint polls so the collector can pause every mutator. Explicitly out of v1.
- **Thread pools / work-stealing / `parallel_map`.** Pointless under the GRL
  (no compute parallelism to exploit); revisit after v2.
- **Per-thread schedulers and cross-thread actor calls.** The scheduler is
  module-global (`run_queue` etc.); making it instantiable is its own project. v1
  keeps exactly one scheduler on the main thread.
- **Structured concurrency (`scope`/nursery), `RWLock`, `--race` detector.**
  Layered on the primitives later.
- **A borrow checker / static `Send`/`Sync`.** Stay with the existing advisory
  `Sendable` warning; no type-system change.

---

## 10. Testing strategy

Follow `tools/run_tests.sh` conventions (real compile+link+run).

- `test/pass/thread_spawn_join.sf` — spawn N threads that each return a value;
  join; assert results.
- `test/pass/thread_mutex_counter.sf` — shared-counter test. Under the GRL a
  plain increment is already atomic, so to make this a *real* mutex test the
  critical section must span a GRL release point (increment around a
  `Thread.sleep`), proving the mutex serializes even when the GRL is dropped.
- `test/pass/thread_channel.sf` — producer/consumer; assert every message arrives
  once, in order, and `recv()` returns nil after close.
- `test/pass/thread_atomic.sf` — CAS loop from multiple threads; assert final
  value.
- `test/pass/async_spawn_blocking.sf` — inside a coroutine, `spawn_blocking` a
  function that `Thread.sleep`s; assert other coroutines make progress meanwhile
  (timestamp interleave), proving the scheduler is not frozen. Validates §5.
- `test/fail/thread_capture_list.sf` — a `Thread.spawn` closure capturing a
  mutable List should emit the non-Sendable warning.
- Determinism: run each threaded test under a loop (e.g. 50×) in CI to shake out
  races — a single green run proves little for concurrency.
- Bootstrap safety: `./bootstrap.sh` must still pass with `@thread` present in
  `src/lib/` (the compiler must not import it).

---

## 11. Workstream sequence (each independently shippable)

- **A — Runtime + spawn/join/detach (GRL).** `thread_native.c` (GRL, trampoline,
  handle table, spawn/join/detach/sleep), `thread.sf` spawn/join, link-line edit,
  GRL acquire at process entry.
- **B — Locks + atomics.** `Thread.Mutex`, `Thread.Atomic`.
- **C — Channels.** `Thread.Channel<T>` (mutex+condvar), close semantics.
- **D — Async bridge.** `Async.await_thread` / `Async.spawn_blocking` via the
  self-pipe→fd→yield-reason-2 trick; wasm synchronous fallback in `thread.sf`.
  This is the headline v1 win — blocking FFI/I/O without freezing the scheduler.
- **E (v2) — True parallelism.** Thread-local shadow stack, allocator mutex,
  thread registry, codegen safepoints, stop-the-world collect; then relax the GRL
  for compute. The large, separate GC project where thread pools become worthwhile.

Workstreams A–D require no GC changes and survive E unchanged.
