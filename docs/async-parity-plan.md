# Async Parity Plan: LLVM Compiler

Bring the native (LLVM-compiled) binaries to feature parity with the C VM's cooperative async system.

## What the C VM Provides

The VM implements stackful cooperative multitasking:

1. **Task.spawn(closure)** — creates a new task (ObjCallFrame) with its own saved stack, adds to round-robin queue
2. **yield** — saves current stack, advances to next task in queue
3. **yield [SLEEP, duration]** — parks task in sleeper queue, wakes after wall-clock time elapses
4. **yield [WAIT_IO_READ, fd]** / **yield [WAIT_IO_WRITE, fd]** — parks task until fd is ready (via select(2))
5. **task.isReady()** — checks FINISHED flag
6. **task.getResult()** — returns completed task's result value
7. **Async.await(task)** — polls isReady() in a yield loop (stdlib, not a primitive)
8. **Async.sleep(duration)** — yield [1, duration] (stdlib wrapper)

The scheduler is a single-threaded round-robin loop. When all tasks are parked, it busy-polls `select(2)` with 200ms timeout until something is ready.

## The Challenge

The VM's approach saves/restores the *entire* value stack per task switch. This works because the VM owns the stack layout. In compiled native code, the OS owns the call stack — you can't arbitrarily save/restore it.

## Approach: LLVM Coroutines

We use LLVM's built-in coroutine intrinsics (`llvm.coro.*`). LLVM automatically transforms yielding functions into state machines — no assembly, no manual stacks, no platform-specific code. The scheduler is pure Saffron.

**Why LLVM coro over stackful coroutines:**

| | LLVM Coro | Stackful (setjmp + mmap) |
|-|-----------|--------------------------|
| Platform code | None | arm64 asm + x86_64 asm |
| Stack management | LLVM handles it | Manual mmap + guard pages |
| Memory per task | Only what's needed (state struct) | Fixed 64KB per task |
| Debugging | Normal stack frames | Opaque swapped stacks |
| Yield restrictions | Must be in coroutine function | None (yield anywhere) |
| Scheduler | Pure Saffron | Needs C or asm |

The only restriction: yield must appear inside a function marked as a coroutine (not from a deeply nested callee). This matches our existing Saffron patterns — `yield` is always written directly in the task function or in thin wrappers like `Async.sleep()`.

---

## Architecture

```
┌──────────────────────────────────────────────────────┐
│  User Saffron code (compiled to LLVM IR)             │
│    yield / Task.spawn(fn) / task.isReady()           │
└────────────────┬─────────────────────────────────────┘
                 │ LLVM coro intrinsics
┌────────────────▼─────────────────────────────────────┐
│  LLVM Coroutine Transform (opt pass)                 │
│    Splits function into init + resume + destroy       │
│    Heap-allocates coroutine frame (state struct)      │
└────────────────┬─────────────────────────────────────┘
                 │ resumed by
┌────────────────▼─────────────────────────────────────┐
│  Scheduler (Saffron, compiled normally)              │
│    Round-robin run queue + sleep/IO parking           │
│    Calls coro.resume(handle) to advance tasks        │
│    Uses select(2) via native wrapper for I/O wait    │
└──────────────────────────────────────────────────────┘
```

---

## How LLVM Coroutines Work

A coroutine function emits this IR pattern:

```llvm
define i8* @my_task() {
entry:
  ; --- Coroutine setup ---
  %id = call token @llvm.coro.id(i32 0, i8* null, i8* null, i8* null)
  %need = call i64 @llvm.coro.size.i64()
  %mem = call i8* @malloc(i64 %need)
  %hdl = call i8* @llvm.coro.begin(token %id, i8* %mem)

  ; --- User code before first yield ---
  call i32 @puts(i8* @.str.hello)

  ; --- Suspend (yield) ---
  %tok = call token @llvm.coro.save(i8* %hdl)
  %susp = call i8 @llvm.coro.suspend(token %tok, i1 false)
  switch i8 %susp, label %unreachable [
    i8 0, label %resume     ; resumed normally
    i8 1, label %cleanup    ; destroyed
  ]

resume:
  ; --- User code after yield ---
  call i32 @puts(i8* @.str.world)
  br label %cleanup

cleanup:
  %mem2 = call i8* @llvm.coro.free(token %id, i8* %hdl)
  call void @free(i8* %mem2)
  br label %done

done:
  call i1 @llvm.coro.end(i8* %hdl, i1 false)
  ret i8* %hdl

unreachable:
  unreachable
}
```

The caller gets back an opaque `i8*` handle. To advance the coroutine:

```llvm
call void @llvm.coro.resume(i8* %hdl)   ; runs until next yield
%finished = call i1 @llvm.coro.done(i8* %hdl)
```

LLVM's coroutine passes (`CoroEarly`, `CoroSplit`, `CoroElide`, `CoroCleanup`) transform this into a state machine at compile time. No runtime support needed beyond `malloc`/`free`.

---

## Phase 1: Coroutine Emission (codegen.sf)

### 1.1 AST Changes

The parser needs a `Yield(expr)` expression node. Check if it exists; if not, add it to `ast.sf` and parse `yield` / `yield expr` in `parser.sf`.

### 1.2 Detecting Coroutine Functions

A function is a coroutine if it (or any function it's the spawn target of) contains a `yield`. The codegen marks these during a pre-pass.

Two approaches:
- **Explicit**: Only `Task.spawn(fn)` targets are coroutines. The codegen wraps them.
- **Implicit**: Any function containing `yield` gets the coro preamble.

**Recommendation: Implicit.** Any function with `yield` in its body gets compiled as a coroutine. This matches the VM semantics where any function can yield.

### 1.3 Coroutine Codegen Pattern

When `gen_fun_decl` encounters a function containing yield:

1. **Preamble**: emit `llvm.coro.id` + `llvm.coro.begin`
2. **Each yield**: emit `llvm.coro.save` + `llvm.coro.suspend` + switch
3. **Function exit**: emit `llvm.coro.end` + cleanup
4. **Return type**: `i8*` (the coroutine handle), not the user's return type
5. **Return value**: Stored into the coroutine frame before final suspend; retrieved via `sf_task_result`

### 1.4 Yield With Arguments (sleep, I/O)

`yield [1, duration]` needs to communicate the yield reason to the scheduler. Two options:

**Option A: Side-channel global** (simpler)
```llvm
@__yield_reason = global i64 0     ; 0=bare, 1=sleep, 2=io_read, 4=io_write
@__yield_arg = global i64 0        ; duration-as-bits or fd

; Before suspending:
store i64 1, i64* @__yield_reason
store i64 %duration_bits, i64* @__yield_arg
; Then suspend
```
The scheduler reads these globals after `coro.resume` returns.

**Option B: Promise-based** (LLVM-native)

Use `llvm.coro.promise` to get a pointer into the coroutine frame where yield metadata is stored. More correct but more complex IR.

**Recommendation: Option A.** Globals are trivial. Single-threaded scheduler means no races.

### 1.5 Task.spawn Codegen

`Task.spawn(fn)` compiles to:
```llvm
; Call the coroutine function — it returns immediately with a handle
%hdl = call i8* @spawned_fn()
; Register with scheduler
call void @__sched_enqueue(i8* %hdl)
; Return handle as task (i64)
%task = ptrtoint i8* %hdl to i64
```

### 1.6 task.isReady() / task.getResult()

```llvm
; isReady — just check if coroutine is done
%done = call i1 @llvm.coro.done(i8* %hdl)
%result = zext i1 %done to i64

; getResult — read from a known offset in the coro frame (or global)
%result = call i64 @__task_get_result(i8* %hdl)
```

For `getResult`, we store the return value into a global or promise slot before the final suspend.

---

## Phase 2: Scheduler (pure Saffron)

The scheduler runs in the main function after all top-level code. It's a normal (non-coroutine) function.

```saffron
// src/lib/scheduler.sf

var run_queue: List<Int> = []
var sleep_queue: List<Int> = []
var sleep_times: List<Number> = []
var io_read_queue: List<Int> = []
var io_read_fds: List<Int> = []
var io_write_queue: List<Int> = []
var io_write_fds: List<Int> = []

fun enqueue(handle: Int) {
    run_queue.push(handle)
}

fun scheduler_run() {
    while (run_queue.length() > 0 or sleep_queue.length() > 0 or io_read_queue.length() > 0) {
        // Wake expired sleepers
        var now = Time.now()
        var i = 0
        while (i < sleep_queue.length()) {
            if (sleep_times[i] <= now) {
                run_queue.push(sleep_queue[i])
                sleep_queue.remove(i)
                sleep_times.remove(i)
            } else {
                i = i + 1
            }
        }

        // Poll I/O if nothing runnable
        if (run_queue.length() == 0) {
            poll_io(200)  // native: select(2) with 200ms timeout
            continue
        }

        // Run next task
        var hdl = run_queue[0]
        run_queue.remove(0)
        coro_resume(hdl)

        if (coro_done(hdl)) {
            coro_destroy(hdl)
        } else {
            // Check yield reason (from globals)
            var reason = get_yield_reason()
            if (reason == 0) {
                run_queue.push(hdl)           // bare yield, re-enqueue
            } else if (reason == 1) {
                sleep_queue.push(hdl)
                sleep_times.push(now + get_yield_arg_float())
            } else if (reason == 2) {
                io_read_queue.push(hdl)
                io_read_fds.push(get_yield_arg_int())
            } else if (reason == 4) {
                io_write_queue.push(hdl)
                io_write_fds.push(get_yield_arg_int())
            }
            reset_yield_reason()
        }
    }
}
```

### Native Helpers (tiny, in runtime.ll or a small .c file)

```c
// Only things that can't be done in Saffron:
double sf_time_now();                              // clock_gettime wrapper
int sf_select(int nfds, fd_set*, fd_set*, int ms); // select(2) wrapper
```

Everything else — queue management, scheduling decisions, task lifecycle — stays in Saffron.

---

## Phase 3: Async I/O Integration

Replace poll-loops in `socket.sf`:

```saffron
fun read(n: Int): String {
    while (true) {
        var bytes = sf_tcp_read(this.fd, buf, n)
        if (bytes > 0) { return result }
        if (bytes == 0) { return "" }       // EOF
        if (bytes == -2) { return "" }      // error
        // bytes == -1: would_block
        yield [2, this.fd]                  // park until fd readable
    }
}
```

The scheduler picks this up via `__yield_reason == 2`, parks the task, and uses `select(2)` to wake it.

---

## Phase 4: Channels

Channels can be pure Saffron once yield works:

```saffron
class Channel {
    var buffer: List<Int>
    var capacity: Int
    var closed: Int

    fun init(cap: Int) {
        this.buffer = []
        this.capacity = cap
        this.closed = 0
    }

    fun send(value: Int) {
        while (this.buffer.length() >= this.capacity) {
            yield  // back-pressure: wait for space
        }
        this.buffer.push(value)
    }

    fun recv(): Int {
        while (this.buffer.length() == 0) {
            if (this.closed) { return 0 }
            yield  // wait for data
        }
        return this.buffer.remove(0)
    }

    fun close() { this.closed = 1 }
    fun has_data(): Int { return this.buffer.length() > 0 }
}
```

No native code needed — channels are just cooperative data structures.

---

## Codegen Changes Summary

### New IR intrinsic declarations (emitted in every coroutine-containing module)

```llvm
declare token @llvm.coro.id(i32, i8*, i8*, i8*)
declare i64 @llvm.coro.size.i64()
declare i8* @llvm.coro.begin(token, i8*)
declare token @llvm.coro.save(i8*)
declare i8 @llvm.coro.suspend(token, i1)
declare i8* @llvm.coro.free(token, i8*)
declare i1 @llvm.coro.end(i8*, i1)
declare void @llvm.coro.resume(i8*)
declare i1 @llvm.coro.done(i8*)
declare void @llvm.coro.destroy(i8*)
```

### Yield reason globals

```llvm
@__yield_reason = global i64 0
@__yield_arg = global i64 0
```

### codegen.sf modifications

1. **Pre-pass**: scan function body for `Yield` nodes → mark as coroutine
2. **gen_fun_decl**: if coroutine, emit coro preamble/cleanup instead of normal entry/ret
3. **gen_yield**: emit store to `@__yield_reason`/`@__yield_arg`, then `coro.save` + `coro.suspend` + switch
4. **gen_call** for `Task.spawn`: call the coro function (returns handle), enqueue it
5. **gen_method_call** for `.isReady()`: emit `@llvm.coro.done`
6. **gen_method_call** for `.getResult()`: emit load from result slot

---

## File Layout

```
src/compiler/
    ast.sf           — add Yield(expr) if missing
    parser.sf        — parse yield / yield expr
    codegen.sf       — coroutine emission, yield reason globals
src/lib/
    scheduler.sf     — pure Saffron scheduler (run queue + sleep + I/O polling)
    async.sf         — unchanged (sleep, await wrappers)
src/runtime.ll       — add @__yield_reason, @__yield_arg globals
src/runtime/
    time_native.c    — sf_time_now() (if not already available)
    select_native.c  — sf_select() wrapper (~20 lines)
```

---

## Build Changes

```bash
# Standard build (no new .c files needed for basic async)
clang -O2 -o program program.ll src/runtime.ll

# With I/O support (Phase 3)
clang -O2 -c -o select_native.o src/runtime/select_native.c
clang -O2 -o program program.ll src/runtime.ll select_native.o
```

**LLVM pass pipeline**: The coroutine passes must run. With `clang -O2` they're included automatically. With `opt` manually: `-passes='coro-early,coro-split,coro-elide,coro-cleanup'`.

---

## Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| Yield from nested callee (not the coro function itself) | Document limitation; match VM patterns where yield is always in the task fn or a direct wrapper |
| LLVM coro pass version differences | Pin to LLVM 15+; the coro passes are stable since LLVM 14 |
| Coroutine frame size unknown at emit time | Use `@llvm.coro.size.i64()` — LLVM fills it in |
| Result value passing | Store in global `@__task_result` before final suspend; scheduler reads it |
| Multiple concurrent tasks writing yield_reason | Single-threaded scheduler means only one task runs at a time — no race |
| Closures as spawn targets | Closure = function pointer + captured env; works as-is in codegen |

---

## Implementation Order

1. **ast.sf + parser.sf** — Ensure `Yield(expr)` node exists and is parsed
2. **codegen.sf: coroutine detection** — Pre-pass to identify functions containing yield
3. **codegen.sf: coro preamble/suspend/cleanup** — Emit the full LLVM coro pattern
4. **codegen.sf: Task.spawn** — Call coro fn, get handle, enqueue
5. **runtime.ll** — Add `@__yield_reason`, `@__yield_arg` globals
6. **scheduler.sf** — Pure Saffron scheduler with run queue
7. **Test** — Hand-write a .ll coroutine, verify it works with `clang -O2`
8. **Integration test** — `test/async_coop.sf` produces same output natively as VM
9. **Phase 3** — Socket I/O yield integration
10. **Phase 4** — Channel class

---

## Success Criteria

| Milestone | Test |
|-----------|------|
| Coro IR is valid | Hand-written .ll with coro intrinsics compiles and runs |
| Codegen emits valid coro | Simple yield function compiles via saffronc → runs |
| Scheduler works | Two tasks alternate printing (interleaved output) |
| spawn + yield parity | `test/async_coop.sf` same output natively as on VM |
| sleep parity | `test/async.sf` same output (allow ±50ms timing variance) |
| I/O parity | HTTP client with concurrent connections |
| Channel parity | Buffered channel send/recv with producer/consumer tasks |
