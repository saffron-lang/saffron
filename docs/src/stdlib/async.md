# Async

```saffron
import "@async" as Async
```

Cooperative multitasking utilities. See also the [Async tutorial](../tutorial/async.md).

## Functions

| Function | Description |
|----------|-------------|
| `Async.await(task)` | Block until task completes, return its result |
| `Async.sleep(seconds)` | Suspend current task for a duration |
| `Async.await_all(tasks)` | Await a list of tasks, return list of results |

## Task spawning

Tasks are spawned via the built-in `Task` object:

```saffron
var t = Task.spawn(fun () => expensive_computation())
var result = Async.await(t)
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

var results = Async.await_all(tasks)
IO.println(results)  // ["result_1", "result_2", "result_3"]
```
