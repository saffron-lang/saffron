# Threading Support for Saffron

**Status:** Draft  
**Date:** 2026-06-03  
**Author:** —  
**Depends on:** Cooperative async (implemented), GC shadow stack, LLVM native compilation

---

## 1. Motivation

Saffron currently provides cooperative concurrency through coroutines (`Task.spawn`, `Async.await`, a single-threaded scheduler). This model is excellent for I/O-bound workloads — async networking, timers, interleaved task coordination — but it has fundamental limitations:

**CPU-bound parallelism.** A single-threaded scheduler cannot utilize multiple cores. A compute-heavy task monopolizes the entire runtime until it yields. There is no way to run `matrix_multiply()` on core 2 while `parse_json()` runs on core 3.

**Blocking FFI.** When Saffron calls into C code via `@extern` (e.g., a synchronous database driver, a compression library), the entire scheduler freezes. All other tasks stall until the FFI call returns.

**I/O parallelism ceiling.** While async I/O handles many concurrent connections well, some workloads benefit from true parallel I/O — multiple threads each performing synchronous reads from different files or devices.

**Background work.** Long-running background tasks (GC compaction, log rotation, telemetry flushing) ideally run on separate threads without impacting latency of the main event loop.

**Real-world demand.** Every major modern language provides threading: Go (goroutines on OS threads), Rust (std::thread + async runtimes), Kotlin (coroutines + Dispatchers), Python (threading + multiprocessing). Saffron needs a story here to be credible for systems-adjacent work.

---

## 2. Threading Model Options

### 2.1 OS Threads (1:1 mapping)

Each Saffron "thread" maps directly to one OS thread (pthread on Unix, Win32 thread on Windows).

| Pros | Cons |
|------|------|
| Simple implementation — just call `pthread_create` | Heavy: each thread costs ~1-8 MB stack | 
| Preemptive scheduling by the OS | Limited count: hundreds, not millions |
| Full parallelism for CPU-bound work | Context switch cost (~1-10 us) |
| Mature debugging tools (gdb, valgrind) | Shared-memory hazards (data races) |

**Examples:** Java (pre-virtual-threads), C, C++, Rust `std::thread`.

### 2.2 Green Threads (M:N mapping)

M user-space "fibers" multiplexed onto N OS threads by a runtime scheduler.

| Pros | Cons |
|------|------|
| Lightweight: thousands to millions of fibers | Complex runtime scheduler |
| Small stacks (2-8 KB, growable) | Stack overflow detection is harder |
| Cheap context switches (no syscall) | Blocking FFI still blocks the OS thread |
| Matches Saffron's existing coroutine model | Debugging is harder (no OS thread per task) |

**Examples:** Go goroutines, Erlang/BEAM processes, early Java green threads.

### 2.3 Thread Pool + Async (Hybrid)

Keep the single-threaded async scheduler for I/O and coordination. Offload CPU-bound work to a fixed pool of OS threads. Results flow back via futures/channels.

| Pros | Cons |
|------|------|
| Best of both worlds | Two execution models to understand |
| Async for I/O, threads for compute | Need safe hand-off between scheduler and pool |
| Bounded resource usage (fixed pool size) | Potential for deadlock if pool saturates |
| Matches existing Saffron architecture well | Still need synchronization primitives |

**Examples:** Rust Tokio (`spawn_blocking`), Python `concurrent.futures`, Node.js worker_threads.

### 2.4 Structured Concurrency

All concurrent work is scoped: child tasks cannot outlive their parent scope. Cancellation propagates. Errors bubble up.

| Pros | Cons |
|------|------|
| No leaked threads/tasks | More restrictive than raw threads |
| Automatic cleanup on error | Some patterns (background daemons) need escape hatches |
| Easier to reason about lifetime | Requires language-level scope integration |
| Composes cleanly with async | |

**Examples:** Kotlin `coroutineScope`, Swift `TaskGroup`, Java Loom `StructuredTaskScope`, Trio (Python).

### 2.5 Recommendation for Saffron

**Phase 1: OS threads (1:1) with a thread-pool helper** — this is the pragmatic choice:

1. We already have coroutines for lightweight concurrency. We don't need green threads for "millions of tasks" — coroutines handle that.
2. What we lack is *parallelism*. OS threads give us that directly.
3. Implementation is straightforward: `pthread_create`/`pthread_join` via `@extern` calls, which we already support.
4. A thread pool built on top provides ergonomic CPU-bound parallelism.

**Phase 2: Structured concurrency** — once basic threads work, add scoped semantics for safety.

We explicitly do NOT pursue M:N green threads. The complexity of a userspace scheduler that migrates coroutines across OS threads is enormous (stack copying, thread-local storage, work stealing). Saffron's coroutines already fill the "lightweight concurrency" niche.

---

## 3. Proposed API

### 3.1 Basic Thread Spawn/Join

```saffron
import "@thread" as Thread

// Spawn a new OS thread running a function
var t = Thread.spawn(fun (): String {
    var result = expensive_fft(signal_data)
    return result
})

// Do other work on the main thread...
process_ui_events()

// Block until the thread completes and get its result
var fft_result: String = t.join()
```

The `Thread.spawn(fn)` call:
- Creates a new OS thread via `pthread_create`
- The function runs to completion on that thread
- Returns a `ThreadHandle<T>` where T is the return type of fn
- The thread has its own GC shadow stack (see Section 5)

### 3.2 Thread Pool

```saffron
import "@thread" as Thread

// Create a pool with N worker threads (defaults to CPU count)
var pool = Thread.pool(4)

// Submit work — returns a Future<T>
var f1 = pool.submit(fun (): Int => fibonacci(40))
var f2 = pool.submit(fun (): Int => fibonacci(41))
var f3 = pool.submit(fun (): Int => fibonacci(42))

// Get results (blocks calling thread until ready)
IO.println("fib(40) = ${f1.get()}")
IO.println("fib(41) = ${f2.get()}")
IO.println("fib(42) = ${f3.get()}")

// Shutdown: wait for all submitted work to finish, then terminate workers
pool.shutdown()
```

### 3.3 Parallel Map (convenience)

```saffron
import "@thread" as Thread

var images = load_image_paths()

// Process all images in parallel using a thread pool internally
// Spawns min(items.length(), cpu_count) threads
var thumbnails = Thread.parallel_map(images, fun (path: String): Image {
    return resize_image(load_image(path), 128, 128)
})
```

### 3.4 Detached Threads (daemon/background)

```saffron
import "@thread" as Thread

// Detached thread: runs in background, cannot be joined
// Terminates when the main program exits
Thread.detach(fun () {
    while (true) {
        flush_telemetry()
        Thread.sleep(5.0)  // OS-level sleep, not async yield
    }
})
```

### 3.5 Thread-Local Storage

```saffron
import "@thread" as Thread

// Declare a thread-local variable with an initializer
var request_id = Thread.local(fun () => "none")

Thread.spawn(fun () {
    request_id.set("req-42")
    handle_request()  // can read request_id.get() anywhere in this thread
}).join()

IO.println(request_id.get())  // "none" — main thread unaffected
```

---

## 4. Shared State and Synchronization

### 4.1 Default: Shared Heap

All threads share a single heap. This is the simplest model and matches what users expect from languages like Java, Go, and C. The tradeoff is that mutable shared state requires synchronization.

**Why not per-thread heaps?** Per-thread heaps (Erlang-style) would eliminate data races but require copying data between threads. This is incompatible with Saffron's existing object model — users expect to pass objects to threads and mutate shared state. Copying semantics would be a major departure.

### 4.2 Mutex (thread-safe)

The existing `@sync` Mutex uses cooperative `yield` and is NOT safe across OS threads (it's a spin-on-yield lock for coroutines). We need a real mutex backed by `pthread_mutex_t`:

```saffron
import "@thread" as Thread

var counter = 0
var mu = Thread.Mutex()

var threads = []
for (i in range(10)) {
    threads.push(Thread.spawn(fun () {
        for (j in range(1000)) {
            mu.lock()
            counter = counter + 1
            mu.unlock()
        }
    }))
}
for (t in threads) { t.join() }
IO.println(counter)  // exactly 10000
```

Also provide a scoped API to prevent forgetting to unlock:

```saffron
mu.with(fun () {
    // critical section — lock held for the duration of this closure
    shared_list.push(compute_result())
})
```

### 4.3 ReadWriteLock

For read-heavy workloads where multiple readers can proceed simultaneously:

```saffron
import "@thread" as Thread

var cache = {}
var rwlock = Thread.RWLock()

// Multiple threads can read concurrently
fun lookup(key: String): String {
    rwlock.read_lock()
    var result = cache.get(key)
    rwlock.read_unlock()
    return result
}

// Only one thread can write at a time
fun update(key: String, value: String) {
    rwlock.write_lock()
    cache.set(key, value)
    rwlock.write_unlock()
}
```

### 4.4 Atomic Operations

For lock-free counters and flags:

```saffron
import "@thread" as Thread

var counter = Thread.Atomic(0)

// From any thread:
counter.add(1)          // atomic increment, returns old value
counter.sub(1)          // atomic decrement
var val = counter.load()
counter.store(42)
counter.compare_swap(42, 100)  // CAS: if current == 42, set to 100
```

Atomics only work with integer values (NaN-boxed integers in Saffron are 48-bit, which fits in a 64-bit atomic word).

### 4.5 Cross-Thread Channels

The existing `Channel<T>` in `@sync` uses cooperative `yield` for backpressure. For cross-thread communication, we need channels backed by a mutex + condition variable:

```saffron
import "@thread" as Thread

// Bounded channel — send blocks if buffer is full
var ch = Thread.Channel<String>(100)

// Producer thread
var producer = Thread.spawn(fun () {
    for (i in range(1000)) {
        ch.send("message ${i}")
    }
    ch.close()
})

// Consumer thread
var consumer = Thread.spawn(fun () {
    while (true) {
        var msg = ch.recv()
        if (msg == nil) { break }  // channel closed and drained
        process(msg)
    }
})

producer.join()
consumer.join()
```

Also provide an unbounded variant:

```saffron
var ch = Thread.unbounded_channel<Int>()
// send() never blocks (until OOM)
```

### 4.6 Do We Need Send/Sync Markers?

**Rust's approach:** Types are `Send` (can be moved to another thread) or `Sync` (can be shared between threads via reference). The compiler enforces this statically.

**Saffron's approach: documentation + runtime checks (Phase 1).**

Saffron does not have a borrow checker or lifetime system. Adding `Send`/`Sync` as type-system constraints would be a massive language change. Instead:

- **Immutable values** (Int, Float, Bool, String, nil) are inherently safe to share — they cannot be mutated.
- **Mutable containers** (List, Map, class instances) are NOT safe without synchronization.
- In debug mode, we can add **runtime race detection** (similar to Go's `-race` flag or TSan): track which thread last wrote to an object, warn if another thread reads without synchronization.

For Phase 1 this is sufficient. A `Sendable` interface could be added later:

```saffron
// Future consideration — not Phase 1
interface Sendable {
    fun clone(): Self  // types must be cloneable to cross thread boundary
}
```

---

## 5. GC and Threading

### 5.1 Current GC Architecture

Saffron's GC is mark-sweep with a shadow stack:
- Each function pushes GC roots onto a thread-local shadow stack
- Collection walks the shadow stack + global roots, marks reachable objects, sweeps the rest
- Allocation triggers collection when `bytes_allocated > next_gc`

### 5.2 Multi-Threaded GC Strategy

**Recommended: Stop-the-world with per-thread shadow stacks.**

```
Thread 1: [shadow_stack_1] ---\
Thread 2: [shadow_stack_2] ----+---> Shared Heap
Thread 3: [shadow_stack_3] ---/        |
                                       v
                                   GC (stop-the-world)
```

Implementation:
1. Each thread gets its own shadow stack pointer (stored in thread-local storage via `__thread` or `pthread_key_t`).
2. All threads share a single heap (object allocator).
3. When GC triggers:
   - The triggering thread requests a GC pause
   - All threads are signaled to reach a **safepoint** (a known-good point where shadow stacks are consistent)
   - Once all threads are at safepoints, the GC runs (single-threaded mark-sweep over all shadow stacks + global roots)
   - Threads are resumed

**Safepoints** are inserted at:
- Function prologues (before any allocation)
- Loop back-edges (so long-running loops can be interrupted)
- Allocation sites (natural check point)

In LLVM IR, a safepoint is a load from a global flag + conditional branch to a "park" routine:

```llvm
%gc_flag = load i8, ptr @gc_requested
%need_pause = icmp ne i8 %gc_flag, 0
br i1 %need_pause, label %safepoint, label %continue

safepoint:
  call void @gc_thread_park()  ; blocks until GC completes
  br label %continue

continue:
  ; ... normal code ...
```

### 5.3 Allocation Strategy

Two options for the shared heap:

**Option A: Global allocator with mutex.** Simple but contention-heavy. Every `malloc` for a GC object takes a lock.

**Option B: Thread-local allocation buffers (TLABs).** Each thread gets a chunk of memory (e.g., 64 KB) and allocates bump-pointer-style within it. When the TLAB is exhausted, it requests a new one from the global heap (which requires a lock, but infrequently). This is what the JVM does.

**Recommendation:** Start with Option A (mutex-protected allocator) for simplicity. Move to TLABs as an optimization once threading is stable.

### 5.4 Coroutines on Worker Threads

A coroutine's frame is heap-allocated (by LLVM's coro machinery). If a thread pool worker runs a coroutine:
- The coroutine frame lives on the shared heap — accessible from any thread
- The coroutine's suspend/resume must be synchronized: only one thread should resume a given coroutine at a time
- The per-thread scheduler runs on the main thread; worker threads do NOT run the scheduler

This means: you cannot `yield` from inside a thread pool task. Thread pool tasks run to completion. If you need async inside a thread, spawn a separate scheduler on that thread (see Section 6).

---

## 6. Interaction with Cooperative Async

### 6.1 Awaiting a Thread from a Coroutine

The existing scheduler suspends on `await`. We extend this to suspend on thread results:

```saffron
import "@async" as Async
import "@thread" as Thread

fun main() {
    // Inside a coroutine, await a thread without blocking the scheduler
    var handle = Thread.spawn(fun () => heavy_work())
    
    // This yields the coroutine until the thread completes,
    // allowing other coroutines to run on the main thread
    var result = Async.await_thread(handle)
    
    IO.println("Got: ${result}")
}
```

Implementation: `Async.await_thread` registers the thread handle with the scheduler. Each scheduler tick, it polls `pthread_tryjoin_np` (or checks a completion flag set by the thread). When done, the waiting coroutine is re-enqueued.

### 6.2 Running a Scheduler on a Separate Thread

For applications that need async I/O on multiple threads (e.g., a server handling connections across cores):

```saffron
import "@thread" as Thread
import "@async" as Async

// Each thread gets its own scheduler instance
var io_thread = Thread.spawn(fun () {
    var sched = Async.Scheduler()
    
    sched.spawn(fun () {
        var conn = accept_connection(server_fd)
        handle_client(conn)
    })
    
    sched.run()  // event loop on this thread
})
```

This requires making the scheduler re-entrant (currently it uses module-level globals). We would need to refactor `@scheduler` into a class:

```saffron
class Scheduler {
    var run_queue: List<Int>
    var sleep_queue: List<Int>
    // ...
    fun spawn(fn: Fun) { ... }
    fun run() { ... }
}
```

### 6.3 Bridge: spawn_blocking

Inspired by Tokio's `spawn_blocking`, allow coroutines to offload blocking work without freezing the scheduler:

```saffron
import "@async" as Async

fun fetch_from_db(query: String): String {
    // This runs on a background thread, but looks like an async call
    // The coroutine suspends; other coroutines keep running on the main thread
    return Async.spawn_blocking(fun () => {
        return db.query_sync(query)  // blocking FFI call
    })
}
```

Internally, `spawn_blocking` does:
1. Submit the closure to a thread pool
2. Suspend the current coroutine (yield reason = "awaiting thread")
3. When the thread completes, the scheduler re-enqueues the coroutine with the result

---

## 7. Safety Model

### 7.1 Race Condition Prevention Strategy

Saffron takes a **pragmatic** approach — not as strict as Rust (no borrow checker), not as lax as C (no undefined behavior from races):

**Tier 1: Safe by default (immutable values)**
- Int, Float, Bool, String, nil — these are immutable. Reading them from any thread is always safe.
- Enum variants with only immutable fields are also safe.

**Tier 2: Protected containers**
- List, Map — NOT safe for concurrent mutation.
- Concurrent reads without writes ARE safe (no internal mutation on read).
- Rule: if you share a mutable container across threads, you MUST use a Mutex or Channel.

**Tier 3: Runtime race detection (debug mode)**

When compiled with `--race` (or `SAFFRON_RACE=1`), the runtime tracks:
- For each mutable object: which thread last wrote to it, and a logical timestamp
- On access: if the current thread differs from the last writer AND no lock is held, emit a warning

```
WARNING: data race detected
  Object: List at 0x7f3a2b4c
  Written by: Thread-2 at scheduler.sf:45
  Read by:    Thread-1 at main.sf:12
  No synchronization observed between these accesses
```

This is advisory (like Go's race detector) — it does not prevent the race, but it catches it during testing.

### 7.2 What Happens on a Race?

Without `--race` mode, Saffron does NOT guarantee any specific behavior on unsynchronized access. The program may:
- See stale values
- See partially-written objects (e.g., a List mid-resize)
- Crash (segfault from corrupted pointers)

This is the same model as Go and Java (before Java's memory model spec). We document it clearly: **if you share mutable state between threads, use synchronization.**

### 7.3 Recommended Patterns

```saffron
import "@thread" as Thread

// PATTERN 1: Message passing (safest)
var ch = Thread.Channel<WorkItem>(100)
Thread.spawn(fun () {
    while (true) {
        var item = ch.recv()
        if (item == nil) { break }
        process(item)
    }
})
ch.send(WorkItem("task1"))

// PATTERN 2: Mutex-protected shared state
var state = SharedState()
var mu = Thread.Mutex()
Thread.spawn(fun () {
    mu.with(fun () {
        state.update(new_data)
    })
})

// PATTERN 3: Fork-join with no sharing
// Each thread works on independent data, results collected at join
var results = Thread.parallel_map(items, fun (item) => {
    // No shared state accessed here — purely functional
    return transform(item)
})
```

---

## 8. Implementation

### 8.1 LLVM IR Thread Primitives

Thread creation via pthreads. The `@thread` stdlib module wraps these `@extern` declarations:

```saffron
// Low-level extern bindings (internal to @thread module)
@extern("i64 pthread_create(i64*, i64*, i64, i64)")
fun _pthread_create(thread_ptr: Int, attr: Int, fn_ptr: Int, arg: Int): Int

@extern("i64 pthread_join(i64, i64*)")
fun _pthread_join(thread: Int, retval_ptr: Int): Int

@extern("i64 pthread_mutex_init(i64*, i64*)")
fun _mutex_init(mutex_ptr: Int, attr: Int): Int

@extern("i64 pthread_mutex_lock(i64*)")
fun _mutex_lock(mutex_ptr: Int): Int

@extern("i64 pthread_mutex_unlock(i64*)")
fun _mutex_unlock(mutex_ptr: Int): Int

@extern("i64 pthread_mutex_destroy(i64*)")
fun _mutex_destroy(mutex_ptr: Int): Int
```

### 8.2 Thread Entry Wrapper

Each spawned thread needs its own GC shadow stack. The thread entry function (in C runtime support):

```c
// runtime/thread_support.c

typedef struct {
    int64_t (*fn)(int64_t);  // Saffron closure (function pointer)
    int64_t arg;             // Captured environment
    int64_t *result_slot;    // Where to store the return value
} ThreadStartArg;

void *saffron_thread_entry(void *raw_arg) {
    ThreadStartArg *tsa = (ThreadStartArg *)raw_arg;
    
    // Initialize per-thread GC shadow stack
    gc_shadow_stack_init();
    
    // Register this thread with the GC (for stop-the-world)
    gc_register_thread(pthread_self());
    
    // Run the Saffron function
    int64_t result = tsa->fn(tsa->arg);
    *tsa->result_slot = result;
    
    // Unregister from GC
    gc_unregister_thread(pthread_self());
    
    // Tear down shadow stack
    gc_shadow_stack_destroy();
    
    free(tsa);
    return NULL;
}
```

### 8.3 Thread-Local GC Shadow Stack

Currently the shadow stack is a global:

```c
// Current (single-threaded)
static int64_t shadow_stack[SHADOW_STACK_SIZE];
static int shadow_stack_top = 0;
```

With threading, this becomes thread-local:

```c
// Multi-threaded
_Thread_local int64_t shadow_stack[SHADOW_STACK_SIZE];
_Thread_local int shadow_stack_top = 0;
```

Or dynamically allocated per thread (to allow configurable size):

```c
typedef struct {
    int64_t *stack;
    int top;
    int capacity;
} ShadowStack;

_Thread_local ShadowStack *tl_shadow_stack = NULL;
```

### 8.4 Stop-the-World Implementation

```c
// gc_threading.c

static pthread_mutex_t gc_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gc_cond = PTHREAD_COND_INITIALIZER;
static volatile int gc_requested = 0;
static volatile int threads_at_safepoint = 0;
static int total_threads = 1;  // main thread

// Called by threads at safepoints
void gc_safepoint_check(void) {
    if (!gc_requested) return;  // fast path: no GC pending
    
    __atomic_add_fetch(&threads_at_safepoint, 1, __ATOMIC_SEQ_CST);
    
    // Park until GC is done
    pthread_mutex_lock(&gc_lock);
    while (gc_requested) {
        pthread_cond_wait(&gc_cond, &gc_lock);
    }
    pthread_mutex_unlock(&gc_lock);
    
    __atomic_sub_fetch(&threads_at_safepoint, 1, __ATOMIC_SEQ_CST);
}

// Called by the GC thread to stop the world
void gc_stop_the_world(void) {
    gc_requested = 1;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    
    // Wait for all threads to reach safepoints
    while (__atomic_load_n(&threads_at_safepoint, __ATOMIC_SEQ_CST) 
           < total_threads - 1) {
        // spin (or use futex for efficiency)
    }
}

// Called after GC completes
void gc_resume_the_world(void) {
    gc_requested = 0;
    pthread_cond_broadcast(&gc_cond);
}
```

### 8.5 Mutex Implementation (Saffron-level)

The `Thread.Mutex` class wraps a heap-allocated `pthread_mutex_t`:

```saffron
class Mutex {
    var _handle: Int  // opaque pointer to pthread_mutex_t

    fun init() {
        this._handle = _alloc_mutex()
        _mutex_init(this._handle, 0)
    }

    fun lock() {
        _mutex_lock(this._handle)
    }

    fun unlock() {
        _mutex_unlock(this._handle)
    }

    fun with(fn: Fun) {
        this.lock()
        try {
            fn()
        } finally {
            this.unlock()
        }
    }

    // Called by GC finalizer
    fun _finalize() {
        _mutex_destroy(this._handle)
        _free_mutex(this._handle)
    }
}
```

### 8.6 Atomic Operations via LLVM

LLVM provides atomic instructions directly:

```llvm
; Atomic increment
%old = atomicrmw add ptr %counter, i64 1 seq_cst

; Compare-and-swap
%result = cmpxchg ptr %counter, i64 %expected, i64 %new seq_cst seq_cst
%success = extractvalue {i64, i1} %result, 1
```

The codegen emits these for `Atomic.add()`, `Atomic.compare_swap()`, etc.

### 8.7 Cross-Thread Channel (internal structure)

```c
typedef struct {
    int64_t *buffer;
    int capacity;
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    int closed;
} ThreadChannel;
```

`send()` locks, waits on `not_full` if buffer is at capacity, enqueues, signals `not_empty`.  
`recv()` locks, waits on `not_empty` if buffer is empty (or returns nil if closed), dequeues, signals `not_full`.

---

## 9. Structured Concurrency

### 9.1 Scoped Threads

Inspired by Rust's `std::thread::scope` and Kotlin's `coroutineScope`:

```saffron
import "@thread" as Thread

fun process_batch(items: List<Item>): List<Result> {
    // All threads spawned in this scope MUST complete before scope exits
    // If any thread throws, all others are cancelled
    return Thread.scope(fun (s: Scope) {
        var a = s.spawn(fun () => process_chunk(items[0..50]))
        var b = s.spawn(fun () => process_chunk(items[50..100]))
        
        return [a.join(), b.join()]
    })
    // At this point, threads a and b are guaranteed to have finished
}
```

### 9.2 Cancellation

Threads in a scope can be cancelled when an error occurs:

```saffron
Thread.scope(fun (s: Scope) {
    var fetcher = s.spawn(fun () {
        // Periodically check if cancelled
        while (!Thread.is_cancelled()) {
            var data = fetch_next_page()
            buffer.push(data)
        }
    })
    
    var processor = s.spawn(fun () {
        process(buffer)
        if (error_detected) {
            throw "processing failed"
        }
    })
    
    // If processor throws, fetcher is cancelled automatically
})
```

Cancellation is cooperative (like Go context cancellation): the cancelled thread must check `Thread.is_cancelled()` at logical checkpoints. We do NOT forcibly terminate threads (unsafe, can leave resources in bad state).

### 9.3 Nursery Pattern (Trio-inspired)

A stricter variant where the scope itself manages task lifecycle:

```saffron
Thread.nursery(fun (n: Nursery) {
    n.spawn(fun () => download(url1))
    n.spawn(fun () => download(url2))
    n.spawn(fun () => download(url3))
    // Nursery waits for ALL children before exiting
    // First exception cancels the rest
})
// Here: all downloads are done (or one failed and rest were cancelled)
```

---

## 10. Implementation Plan

### Phase 1: Basic OS Threads (MVP)

**Scope:** `Thread.spawn(fn)`, `Thread.join()`, `Thread.Mutex`, `Thread.sleep`

**Work items:**
- [ ] Add `runtime/thread_support.c` with thread entry wrapper
- [ ] Make shadow stack thread-local (`_Thread_local`)
- [ ] Implement `@thread` stdlib module with spawn/join/mutex
- [ ] Thread handle as a GC-managed object (so handles aren't leaked)
- [ ] Basic test: spawn threads, join them, verify results
- [ ] Test: mutex protects a shared counter correctly

**Estimated effort:** 2-3 weeks

### Phase 2: Thread Pool + Parallel Map

**Scope:** `Thread.pool(n)`, `pool.submit(fn)`, `Future.get()`, `Thread.parallel_map`

**Work items:**
- [ ] Implement thread pool with work-stealing queue
- [ ] `Future<T>` type with `.get()` (blocks), `.is_ready()` (polls)
- [ ] `parallel_map` convenience function
- [ ] Test: parallel_map produces correct results
- [ ] Test: pool handles more tasks than threads (queuing)
- [ ] Benchmark: parallel_map vs sequential for CPU-bound work

**Estimated effort:** 2 weeks

### Phase 3: Cross-Thread Channels

**Scope:** `Thread.Channel<T>(capacity)`, `.send()`, `.recv()`, `.close()`

**Work items:**
- [ ] Implement bounded channel with mutex + condvar
- [ ] Implement unbounded channel variant
- [ ] `select` over multiple channels (like Go select)
- [ ] Test: producer-consumer pattern
- [ ] Test: multiple producers, single consumer
- [ ] Test: channel close semantics

**Estimated effort:** 2 weeks

### Phase 4: GC Safety

**Scope:** Stop-the-world GC for multi-threaded programs

**Work items:**
- [ ] Safepoint insertion in LLVM codegen (function prologues, loop back-edges)
- [ ] Thread registry (track all live threads for STW)
- [ ] STW implementation (signal, park, collect, resume)
- [ ] Thread-local allocation buffers (TLAB) for reduced contention
- [ ] Test: GC under heavy multi-threaded allocation
- [ ] Test: no use-after-free when thread exits during GC

**Estimated effort:** 3-4 weeks

### Phase 5: Async Integration

**Scope:** `Async.await_thread()`, `Async.spawn_blocking()`, per-thread schedulers

**Work items:**
- [ ] `await_thread` yield reason in scheduler
- [ ] `spawn_blocking` API (submits to internal pool, suspends coroutine)
- [ ] Refactor scheduler into instantiable class (not module globals)
- [ ] Test: mix of coroutines and threads cooperating
- [ ] Test: spawn_blocking doesn't freeze scheduler

**Estimated effort:** 2-3 weeks

### Phase 6: Structured Concurrency

**Scope:** `Thread.scope`, `Thread.nursery`, cancellation

**Work items:**
- [ ] Scope object that tracks child threads
- [ ] Automatic join-all on scope exit
- [ ] Cancellation flag (cooperative)
- [ ] Exception propagation from child to parent scope
- [ ] Test: scope waits for all children
- [ ] Test: exception in one child cancels siblings

**Estimated effort:** 2-3 weeks

### Phase 7: Tooling

**Scope:** Race detector, debugging support

**Work items:**
- [ ] `--race` flag: instrument all object writes with thread + timestamp
- [ ] Detect unsynchronized cross-thread access
- [ ] Thread-aware backtraces (show which thread crashed)
- [ ] Thread names for debugging: `Thread.spawn(fn, name: "worker-1")`

**Estimated effort:** 2-3 weeks

---

## 11. Open Questions

### Shared Heap vs Per-Thread Heap?

**Current recommendation: shared heap.** Per-thread heaps would require:
- Copying objects when passing between threads (expensive, complex for cyclic data)
- A concept of "ownership" that Saffron's type system cannot express
- Rewriting every container type with deep-copy semantics

Shared heap is simpler, matches user expectations, and works with the existing object model. The cost is that we need synchronization and GC coordination — but these are well-understood problems.

### Do We Need a `Send` Marker Type?

**Not in Phase 1.** The priority is getting threads working. A `Send` interface could be added later if data races prove to be a major source of bugs. For now, document which types are safe to share and provide good runtime detection.

### Should List/Map Have Concurrent Variants?

**Yes, eventually.** A `ConcurrentMap` (like Java's `ConcurrentHashMap`) would be useful for caches and registries. But this is a Phase 3+ concern. For Phase 1-2, users wrap `Map` in a `Mutex`.

```saffron
// Future: built-in concurrent map
import "@thread" as Thread

var cache = Thread.ConcurrentMap<String, Response>()
// Safe to read/write from any thread without external locking
cache.set("key", response)
var val = cache.get("key")
```

### How Do Threads Interact with the Existing `@async` Scheduler?

Three interaction modes:
1. **Async-unaware threads** — most common. Thread runs a function, returns a result. No coroutines inside.
2. **Await thread from async** — coroutine suspends until thread completes (`Async.await_thread`).
3. **Per-thread scheduler** — advanced. Thread runs its own event loop. Needed for multi-core servers.

The default scheduler remains single-threaded on the main thread. Only users who explicitly create per-thread schedulers get multi-threaded async.

### Stack Size for Spawned Threads?

Default: 2 MB (same as pthread default on most systems). Allow override:

```saffron
Thread.spawn(fn, stack_size: 8 * 1024 * 1024)  // 8 MB for deep recursion
```

For thread pools, all workers share the same stack size (configurable at pool creation).

### What About the CVM (Bytecode Interpreter)?

This design targets the LLVM-compiled path only. The CVM (C bytecode interpreter) has a fundamentally different execution model (single global VM state, GC tied to the interpreter loop). Adding threading to the CVM would require:
- Making the VM re-entrant (per-thread instruction pointer, stack, call frames)
- A GIL (Global Interpreter Lock) approach like Python, or full VM isolation like Lua lanes

**Recommendation:** Threading is an LLVM-only feature. The CVM remains single-threaded. This is acceptable because: the CVM is primarily for the REPL and rapid prototyping; performance-critical code uses the native compiler.

---

## 12. Comparison with Other Languages

| Feature | Go | Rust | Kotlin | Elixir | Saffron (proposed) |
|---------|-----|------|--------|--------|-------------------|
| Lightweight tasks | goroutines (M:N) | tokio tasks | coroutines | processes | coroutines (existing) |
| OS threads | hidden (runtime manages) | `std::thread` | `Thread` | NIF threads | `Thread.spawn` |
| Thread pool | hidden (GOMAXPROCS) | rayon / tokio | `Dispatchers.Default` | hidden (schedulers) | `Thread.pool(n)` |
| Channels | built-in (`chan`) | `mpsc`, `crossbeam` | `Channel` | mailbox (per-process) | `Thread.Channel` |
| Mutex | `sync.Mutex` | `std::sync::Mutex` | `Mutex` | not needed (no sharing) | `Thread.Mutex` |
| Atomics | `sync/atomic` | `std::sync::atomic` | `AtomicInt` | `:atomics` | `Thread.Atomic` |
| Race detection | `-race` flag | compile-time (borrow checker) | Kotlin/JVM races | impossible (no sharing) | `--race` flag |
| Structured concurrency | `errgroup` (library) | tokio `JoinSet` | `coroutineScope` | supervisor trees | `Thread.scope` |
| GC + threads | concurrent GC | no GC | JVM GC (concurrent) | per-process GC | stop-the-world |
| Blocking FFI | wraps in OS thread auto | just blocks the thread | `Dispatchers.IO` | NIF scheduler | `Async.spawn_blocking` |

### Key Design Decisions (informed by prior art)

**From Go:** The channel-first communication model. "Don't communicate by sharing memory; share memory by communicating." Our `Thread.Channel` is the recommended way to coordinate threads.

**From Rust:** The `scope` API that guarantees no dangling thread references. Our `Thread.scope` ensures all child threads complete before the parent continues.

**From Kotlin:** The integration between coroutines and thread pools via dispatchers. Our `Async.spawn_blocking` achieves the same bridge between cooperative async and thread pool execution.

**From Elixir:** The supervision and restart patterns. While we don't go full actor model, the `Thread.nursery` pattern with automatic cancellation on error is inspired by Erlang's "let it crash" philosophy applied to thread groups.

---

## 13. Example: Complete Multi-Threaded Web Scraper

Putting it all together — a practical example showing threads, channels, async, and synchronization working in concert:

```saffron
import "@thread" as Thread
import "@async" as Async
import "@http" as Http

class Scraper {
    var url_channel: Thread.Channel<String>
    var result_channel: Thread.Channel<Page>
    var visited: Thread.ConcurrentMap<String, Bool>
    var worker_count: Int

    fun init(workers: Int) {
        this.url_channel = Thread.Channel<String>(1000)
        this.result_channel = Thread.Channel<Page>(100)
        this.visited = Thread.ConcurrentMap<String, Bool>()
        this.worker_count = workers
    }

    fun run(seed_urls: List<String>): List<Page> {
        // Seed the URL channel
        for (url in seed_urls) {
            this.url_channel.send(url)
        }

        // Spawn worker threads — each runs its own async scheduler for HTTP
        var workers = []
        for (i in range(this.worker_count)) {
            workers.push(Thread.spawn(fun () => this._worker(i)))
        }

        // Collector: gather results until all workers are done
        var pages: List<Page> = []
        while (true) {
            var page = this.result_channel.recv()
            if (page == nil) { break }
            pages.push(page)
        }

        for (w in workers) { w.join() }
        return pages
    }

    fun _worker(id: Int) {
        while (true) {
            var url = this.url_channel.recv()
            if (url == nil) { break }  // channel closed

            // Skip if already visited
            if (this.visited.get(url) != nil) { continue }
            this.visited.set(url, true)

            // Fetch page (blocking HTTP — each worker is on its own thread)
            var body = Http.get(url)
            var page = Page(url, body)

            // Send result back
            this.result_channel.send(page)

            // Extract and enqueue new URLs
            var links = extract_links(body)
            for (link in links) {
                this.url_channel.try_send(link)  // non-blocking, drop if full
            }
        }
    }
}

// Usage:
var scraper = Scraper(8)
var pages = scraper.run(["https://example.com"])
IO.println("Scraped ${pages.length()} pages")
```

---

## 14. Summary

Saffron's threading design builds **on top of** the existing cooperative async model rather than replacing it:

- **Coroutines** remain the primary tool for I/O concurrency (lightweight, zero-cost switching)
- **OS threads** provide CPU parallelism (the missing piece today)
- **Thread pools** offer ergonomic parallel computation
- **Channels** bridge the two worlds safely
- **Structured concurrency** prevents thread leaks and simplifies error handling
- **Stop-the-world GC** is the pragmatic first approach to multi-threaded memory management

The implementation is incremental: each phase delivers value independently, and later phases build on earlier foundations without breaking changes.
