# Thread

```saffron
import "@thread" as Thread
```

OS threads and cross-thread synchronization. **Native only** — on wasm32 there
are no OS threads.

## The model: one runtime lock

Saffron's garbage collector is not thread-safe (a single global heap and shadow
stack, no locking). So threads run under a **Global Runtime Lock (GRL)**: only one
thread executes managed Saffron code at a time, and the lock is released only
around genuinely-blocking native calls — `join`, `sleep`, a `Mutex.lock`, a
`Channel` send/recv wait.

That buys **blocking-I/O and blocking-FFI concurrency** — a synchronous C call on
a worker no longer freezes everything else — but *not* multi-core CPU parallelism
for pure-Saffron compute. This is not a way to make CPU-bound Saffron faster; it
is a way to keep a program responsive while a thread is blocked in the OS or in C.
For serialized shared state across concurrent tasks, [actors](../tutorial/actors.md)
remain the right tool.

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Thread.spawn(fn)` | `ThreadHandle` | Start `fn` on a new OS thread |
| `Thread.detach(fn)` | — | Spawn and immediately detach: fire-and-forget |
| `Thread.sleep(seconds)` | — | Sleep the current thread (a real OS sleep, releasing the GRL) |

`Thread.sleep` is distinct from `Async.sleep`, which yields a coroutine within the
single-threaded scheduler.

## `ThreadHandle`

| Method | Returns | Description |
|--------|---------|-------------|
| `handle.ok()` | `Bool` | True if the OS actually created the thread |
| `handle.join()` | `Any` | Block until the thread finishes and return its result; a second join, or a join on a detached/failed handle, is `nil` |
| `handle.detach()` | — | Give up the right to join; the thread runs to completion on its own |

## `Thread.Mutex`

A mutual-exclusion lock, separate from the GRL. You need it whenever a critical
section spans a point where the GRL is dropped (a `join`, a `sleep`, another
`lock`): the GRL alone serializes only *uninterrupted* managed execution.

| Method | Returns | Description |
|--------|---------|-------------|
| `Mutex()` | `Mutex` | Create a (non-recursive) lock |
| `m.lock()` | — | Acquire, blocking until available |
| `m.try_lock()` | `Bool` | Acquire without blocking; false if held |
| `m.unlock()` | — | Release |
| `m.with(body)` | `Any` | Run `body` with the lock held, releasing it even if `body` throws |
| `m.free()` | — | Destroy the underlying OS mutex |

## `Thread.Atomic`

A single integer with atomic operations — a lock-free counter or flag. Its value
lives off the managed heap, so it is safe to touch while the GRL is dropped (an
atomic increment around a `sleep` needs no lock).

| Method | Returns | Description |
|--------|---------|-------------|
| `Atomic(initial)` | `Atomic` | Create with an initial value |
| `a.load()` | `Int` | The current value |
| `a.store(v)` | — | Set the value |
| `a.add(delta)` | `Int` | Atomically add and return the **new** value |
| `a.compare_and_set(expected, desired)` | `Bool` | Set to `desired` only if the current value is `expected` |
| `a.free()` | — | Release the slot |

## `Thread.Channel`

A thread-safe FIFO channel — the recommended way to move data across a
`Thread.spawn` boundary. `send` blocks when full (bounded), `recv` blocks when
empty; both wake without busy-looping.

| Method | Returns | Description |
|--------|---------|-------------|
| `Channel(cap)` | `Channel` | Create with buffer capacity `cap` (0 or negative = unbounded) |
| `ch.send(value)` | — | Send; blocks while full; throws if closed |
| `ch.recv()` | `Any` | Receive, blocking until an item is available; `nil` once closed and drained |
| `ch.try_recv()` | `Any` | Receive without blocking; `nil` if currently empty |
| `ch.length()` | `Int` | Items currently buffered |
| `ch.close()` | — | No more sends; wakes every blocked sender/receiver |
| `ch.is_closed()` | `Bool` | True once closed |
| `ch.free()` | — | Release the underlying OS resources |

The buffer is a managed list, so any value is stored safely (strings and other GC
objects included). But a *mutable* container you also keep using on the sender
side is shared, not copied — send immutable values, a fresh container you then
drop, or a [`@copy`](./copy.md) deep copy.

## Awaiting a thread from async code

A blocking C call made directly from a coroutine would freeze the whole
cooperative event loop: the single scheduler thread is stuck in the OS, so no
other coroutine can run until it returns. The async bridge fixes that. It runs
the blocking work on a real OS thread and lets the awaiting coroutine *park*
until the worker finishes, so every other coroutine keeps making progress
meanwhile. The two entry points live on the [Async](./async.md) module:

| Function | Returns | Description |
|----------|---------|-------------|
| `Async.spawn_blocking(fn)` | `ThreadHandle` | Run `fn` on an OS thread; returns a handle to await |
| `Async.await_thread(handle)` | `Any` | Park the current coroutine until the worker finishes, then return its result |

`await_thread` parks on the worker's completion self-pipe via the same IO-read
yield the scheduler uses for sockets, so the event loop keeps running every other
coroutine while this one waits; the worker writes a byte to the pipe when it is
done, the fd poll wakes the coroutine, and it joins (already complete, so
instant) to collect the result. If the worker finished before the coroutine
parked, the byte is already buffered and the poll sees it at once — no lost
wakeup.

`Async.spawn_blocking` is a thin wrapper over `Thread.spawn_piped`, the low-level
primitive that starts a thread *with* a completion self-pipe and returns a
`ThreadHandle` whose `done_fd()` is the fd to park on (`raw_handle()` exposes the
underlying handle). Prefer `Async.spawn_blocking` unless you are building your own
bridge.

### The v1 constraint: `fn` must be pure C or compute

`fn` is for work that **blocks in C** or **computes** — a synchronous FFI call, a
slow syscall, a tight numeric loop. It must **not**, in v1, call a Saffron
primitive that releases the GRL while the async scheduler is running:
`Thread.sleep`, `Thread.Mutex.lock`, another `Thread.join`, or a channel wait.
Doing so lets the worker and the main-thread scheduler execute managed code at
the same time, and they share scheduler globals — so the result comes back
corrupted. Keeping `fn` to pure C/compute keeps the worker off those globals
entirely. Lifting this limitation needs true scheduler/worker isolation, a v2
item.

### The return-value gotcha (BUGS #38)

`await_thread` returns `Any` on purpose. Do **not** stash the result in an
intermediate variable annotated with a concrete type across the suspension point:

```saffron
var r: Int = Async.await_thread(h)   // WRONG — re-tags an already-boxed Any,
                                     // reads back wrong after the coroutine suspends
```

Annotating an already-boxed `Any` as `Int` re-tags it, and once the coroutine
actually suspends (another task runs), the re-tagged value is read back wrong (a
`Float` came out as a denormal). Either return it directly or keep the local
typed `Any`:

```saffron
return Async.await_thread(h)         // right
var r: Any = Async.await_thread(h)   // also right
```

### Example: await a compute worker while a sibling coroutine keeps running

```saffron
import "@thread" as Thread
import "@async" as Async

// A compute worker runs on its own OS thread. The worker does PURE compute —
// no Thread.sleep / lock / join / channel wait inside it (that would race the
// scheduler in v1).
var worker = Task.spawn(fun (): Int {
    var h: Thread.ThreadHandle = Async.spawn_blocking(fun (): Int {
        var acc: Int = 0
        var i: Int = 0
        while (i < 5000000) { acc = acc + (i % 7); i = i + 1 }
        return acc
    })
    return Async.await_thread(h)   // returned directly — see the gotcha above
})

// Meanwhile a sibling coroutine advances a counter via Async.sleep
// (scheduler-side, safe). It keeps ticking while the worker crunches.
var progress = Thread.Atomic(0)
var sibling = Task.spawn(fun (): Int {
    var i = 0
    while (i < 5) {
        Async.sleep(0.005)
        progress.add(1)
        i = i + 1
    }
    return 0
})

worker.await()
sibling.await()
// progress.load() > 0 — the event loop kept running while the worker ran
```

## Example: spawn and join

```saffron
import "@thread" as Thread

var t = Thread.spawn(fun (): Int => 40 + 2)
// ... do other work on this thread ...
var result = t.join()   // 42
```

## Example: producer/consumer over a channel

```saffron
import "@thread" as Thread

var ch = Thread.Channel(4)
Thread.spawn(fun (): Int {
    var i = 0
    while (i < 20) { ch.send(i); i = i + 1 }
    ch.close()
    return 0
})

var v = ch.recv()
while (v != nil) {
    IO.println(v)
    v = ch.recv()
}
```

## Example: a mutex around a GRL-release section

```saffron
import "@thread" as Thread

var m = Thread.Mutex()
var count = Thread.Atomic(0)

fun bump() {
    m.with(fun (): Int {
        var cur = count.load()
        Thread.sleep(0.005)   // GRL released here — the mutex still serializes
        count.store(cur + 1)
        return 0
    })
}
```
