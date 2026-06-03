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

## Task spawning and awaiting

Tasks are spawned via the built-in `Task` object. Await results with `task.await()`:

```saffron
var t = Task.spawn(fun () => expensive_computation())
var result = t.await()
```

## Example: parallel fetch

```saffron
import "@async" as Async

fun fetch(id: Number): String {
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
