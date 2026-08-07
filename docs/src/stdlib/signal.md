# Signal

```saffron
import "@signal" as Signal
```

Receiving POSIX signals. Sending lives in [`@process`](./os.md) (a subprocess's
`kill`/`terminate`); this is the receiving half. **Native only** — a wasm build
has no process signals.

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Signal.on(signum, callback)` | `Bool` | Run `callback` when the process receives `signum` (replaces any previous handler for it) |
| `Signal.wait(signums, timeout_secs)` | `Int` | Block until any of `signums` arrives, or `timeout_secs` elapses; returns the signal number, or `-1` on timeout |
| `Signal.raise(signum)` | `Bool` | Send `signum` to the current process |
| `Signal.init()` | `Int` | Ensure the self-pipe exists; returns its fd, or `-1` if the platform has no signal support |

## Constants

| Name | Meaning |
|------|---------|
| `Signal.SIGINT` | Interrupt (Ctrl-C) |
| `Signal.SIGTERM` | Polite termination request |
| `Signal.SIGHUP` | Terminal hangup / reload convention |
| `Signal.SIGQUIT` | Quit (Ctrl-\\) |
| `Signal.SIGUSR1`, `Signal.SIGUSR2` | Application-defined (their numbers differ by platform — always pass the constant, never a literal) |

## How it works

A POSIX signal handler may call only async-signal-safe functions, so it can never
run Saffron code directly. The runtime handler does the one safe thing — write a
byte into a self-pipe — which turns "a signal arrived" into "a byte is readable on
an fd", something the cooperative scheduler already parks on. So your callback
runs as **ordinary Saffron code between scheduler ticks**, never inside the
interrupt.

## Example: graceful shutdown with a callback

```saffron
import "@signal" as Signal

Signal.on(Signal.SIGINT, fun () {
    IO.println("shutting down cleanly")
    // ... release resources ...
})

// spawn your work; the scheduler runs after main() and delivers the callback
var server = Task.spawn(fun () => serve())
server.await()
```

## Example: wait inline, with a timeout

```saffron
import "@signal" as Signal

var sig = Signal.wait([Signal.SIGINT, Signal.SIGTERM], 30.0)
if (sig == -1) {
    IO.println("timed out")
} else {
    IO.println("got signal ${sig}")
}
```

`Signal.wait` parks the task on the signal fd with a deadline, so it neither
busy-loops nor blocks the whole runtime — other tasks keep running while it waits.
