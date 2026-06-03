# Async and Tasks

Saffron provides cooperative multitasking through lightweight tasks. Tasks run concurrently on a single thread, yielding control at explicit points.

## Spawning tasks

Use `Task.spawn` to create a new task from a function:

```saffron
import "@async" as Async

var task = Task.spawn(fun () {
    IO.println("hello from a task")
    return 42
})
```

The task begins running when the scheduler gets control (at the next yield point or `await`).

## Awaiting results

Use `task.await()` to wait for a task to complete and get its return value:

```saffron
import "@async" as Async

fun compute(): Number {
    Async.sleep(0.1)
    return 42
}

var task = Task.spawn(compute)
var result = task.await()
IO.println(result)  // 42
```

## Async.sleep

`Async.sleep(seconds)` yields the current task for at least the given duration:

```saffron
import "@async" as Async

fun worker(name: String, delay: Number): String {
    Async.sleep(delay)
    return "${name} done"
}
```

## Running tasks concurrently

Spawn multiple tasks and await them all:

```saffron
import "@async" as Async

fun fetch(url: String): String {
    Async.sleep(0.1)
    return "data from ${url}"
}

var t1 = Task.spawn(fun () => fetch("api/users"))
var t2 = Task.spawn(fun () => fetch("api/posts"))
var t3 = Task.spawn(fun () => fetch("api/comments"))

IO.println(t1.await())
IO.println(t2.await())
IO.println(t3.await())
```

All three tasks run concurrently — total time is ~0.1s, not 0.3s.

## Yield

Use `yield` to explicitly give other tasks a chance to run without sleeping:

```saffron
fun long_computation(): Number {
    var sum = 0
    for (var i = 0; i < 1000000; i = i + 1) {
        sum = sum + i
        if (i % 10000 == 0) yield
    }
    return sum
}
```

## Cooperative scheduling

Saffron's async model is cooperative, not preemptive. Tasks only switch at:
- `yield` expressions
- `Async.sleep()` calls
- `task.await()` calls

This means no data races — if you don't yield, you have exclusive access to all state.
