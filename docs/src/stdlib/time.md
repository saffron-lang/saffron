# Time

```saffron
import "@time" as Time
```

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Time.now()` | `Float` | Current Unix timestamp (seconds) |
| `Time.clock()` | `Float` | Monotonic clock (seconds since start) |
| `Time.sleep(seconds)` | — | Block the current thread |

## Example: timing code

```saffron
import "@time" as Time

var start = Time.clock()

// ... do work ...

var elapsed = Time.clock() - start
IO.println("Took ${elapsed}s")
```

## Note on async

For non-blocking sleep in async contexts, use `Async.sleep()` instead of `Time.sleep()`. `Time.sleep()` blocks the entire runtime; `Async.sleep()` only suspends the current task.
