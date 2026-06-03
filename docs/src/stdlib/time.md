# Time

```saffron
import "@time" as Time
```

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Time.now()` | `Float` | Current monotonic time in seconds |
| `Time.clock()` | `Float` | Alias for `now()` |
| `Time.timestamp()` | `Float` | Alias for `now()` |
| `Time.sleep(seconds)` | -- | Block the current thread (busy-wait) |
| `Time.elapsed(start)` | `Float` | Seconds elapsed since `start` |
| `Time.measure(func)` | `Float` | Execute `func` and return elapsed seconds |

## Example: timing code

```saffron
import "@time" as Time

var start = Time.clock()

// ... do work ...

var elapsed = Time.elapsed(start)
IO.println("Took ${elapsed}s")
```

## Example: measuring a function

```saffron
import "@time" as Time

var duration = Time.measure(fun () => expensive_computation())
IO.println("Computation took ${duration}s")
```

## Note on async

For non-blocking sleep in async contexts, use `Async.sleep()` instead of `Time.sleep()`. `Time.sleep()` blocks the entire runtime (busy-wait); `Async.sleep()` only suspends the current task.
