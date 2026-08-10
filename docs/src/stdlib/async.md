# Async

```saffron
import "@async" as Async
```

Cooperative multitasking utilities. See also the [Async tutorial](../tutorial/async.md).

## Functions

| Function | Description |
|----------|-------------|
| `Async.sleep(seconds)` | Suspend current task for a duration |
| `Async.gather(tasks)` | Await a list of tasks, return list of results (like Python's `asyncio.gather`) |
| `Async.race(tasks)` | Return the result of the first task to complete |
| `Async.timeout(fn, seconds)` | Run a function with a timeout; returns result or `0` on timeout |
| `Async.parallel(fns, max_concurrent)` | Run functions concurrently with bounded parallelism |
| `Async.spawn_blocking(fn)` | Run a blocking `fn` on an OS thread; returns a `ThreadHandle` (native only) |
| `Async.await_thread(handle)` | Park the current coroutine until the worker finishes, then return its result (native only) |

## Calling blocking code without freezing the loop

`Async.spawn_blocking` / `Async.await_thread` bridge a blocking C call, syscall,
or CPU-bound loop onto an OS thread so a coroutine can await it without stalling
the event loop. They are **native only** (they use [`@thread`](./thread.md)); on
wasm there are no OS threads.

```saffron
import "@thread" as Thread
import "@async" as Async

var h: Thread.ThreadHandle = Async.spawn_blocking(fun () => slow_c_call())
// ... other coroutines run here ...
return Async.await_thread(h)   // return directly; do not annotate as a concrete type
```

The worker `fn` must be **pure C or compute** — no GRL-releasing Saffron
primitive (`Thread.sleep`, a mutex lock, another `join`, a channel wait) inside
it in v1 — and the result is `Any` for a reason. See the [Thread
page](./thread.md#awaiting-a-thread-from-async-code) for the full constraint and
the return-value gotcha.

## Task spawning and awaiting

Tasks are spawned via the built-in `Task` object. Await results with `task.await()`:

```saffron
var t = Task.spawn(fun () => expensive_computation())
var result = t.await()
```

## Example: parallel fetch

```saffron
import "@async" as Async

fun fetch(id: Int): String {
    Async.sleep(0.1)
    return "result_${id}"
}

var tasks = [
    Task.spawn(fun () => fetch(1)),
    Task.spawn(fun () => fetch(2)),
    Task.spawn(fun () => fetch(3))
]

var results = Async.gather(tasks)
IO.println(results)  // ["result_1", "result_2", "result_3"]
```

## Example: timeout

```saffron
import "@async" as Async

var result = Async.timeout(fun () => slow_operation(), 5.0)
// Returns result if it completes within 5 seconds, otherwise 0
```

## Example: bounded parallelism

```saffron
import "@async" as Async

var fns = [
    fun () => fetch("url1"),
    fun () => fetch("url2"),
    fun () => fetch("url3"),
    fun () => fetch("url4")
]

// Run at most 2 concurrently
var results = Async.parallel(fns, 2)
```
