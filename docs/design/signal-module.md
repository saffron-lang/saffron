# `@signal` Module — OS Signal Handling

## Overview

A stdlib module (`src/lib/signal.sf`) providing safe registration of OS signal handlers, signal masking, and cooperative signal waiting integrated with Saffron's async runtime.

## API

```saffron
import "@signal" as Signal

// Constants (POSIX values)
Signal.SIGINT   // 2
Signal.SIGTERM  // 15
Signal.SIGHUP   // 1
Signal.SIGUSR1  // 10
Signal.SIGUSR2  // 12
Signal.SIGPIPE  // 13

// Register a handler — called asynchronously when signal arrives
Signal.on(Signal.SIGINT, fun () {
    IO.println("caught SIGINT")
    OS.exit(0)
})

// Trap a signal (prevent default, enqueue for wait/poll)
Signal.trap(Signal.SIGTERM)

// Block until one of the listed signals arrives (cooperative yield)
var sig: Int = Signal.wait([Signal.SIGINT, Signal.SIGTERM])

// Ignore a signal entirely (SIG_IGN)
Signal.ignore(Signal.SIGHUP)

// Reset to default OS behavior (SIG_DFL)
Signal.reset(Signal.SIGINT)
```

## Implementation

### Extern declarations (in signal.sf)

```saffron
// sigaction(signum, act, oldact) — we use a C shim for the struct
@extern("i32 _sf_signal_register(i32, i8*)")
fun _register(signum: Int, handler_ptr: Int): Int

@extern("i32 _sf_signal_ignore(i32)")
fun _ignore(signum: Int): Int

@extern("i32 _sf_signal_reset(i32)")
fun _reset(signum: Int): Int

@extern("i32 _sf_signal_poll()")
fun _poll(): Int   // returns pending signal number, or 0
```

### Native shim (`signal_native.c`)

A small C file (~80 lines) linked by the build driver:

- `_sf_signal_register(signum, handler_tag)` — installs a `sigaction` handler that writes the signal number to a lock-free ring buffer (no heap allocation in signal context). Stores the tagged Saffron closure pointer for later dispatch.
- `_sf_signal_ignore(signum)` — calls `signal(signum, SIG_IGN)`.
- `_sf_signal_reset(signum)` — calls `signal(signum, SIG_DFL)`.
- `_sf_signal_poll()` — drains one entry from the ring buffer and returns it (0 if empty).

The ring buffer is a fixed 64-slot atomic array — signals are rare, so overflow is not a practical concern.

### Saffron-side dispatch (in signal.sf)

```saffron
var _handlers: Map<Int, Fun> = {}
var _trapped: Map<Int, Bool> = {}

fun on(signum: Int, handler: Fun) {
    _handlers.set(signum, handler)
    _register(signum, 0)  // 0 = use dispatch loop, not direct call
}

fun trap(signum: Int) {
    _trapped.set(signum, true)
    _register(signum, 0)
}

fun wait(signals: List<Int>): Int {
    for (s in signals) { trap(s) }
    while (true) {
        var sig = _poll()
        if (sig > 0) { return sig }
        __suspend(0, 0)  // yield to scheduler
    }
}
```

A tick hook (or scheduler integration) calls `_poll()` between task switches and invokes registered handlers.

## Async/Thread Interaction

- **Cooperative async:** `Signal.wait()` uses `__suspend` to yield. The scheduler polls `_poll()` on each tick and dispatches pending handlers as microtasks. Handlers never run mid-instruction.
- **Thread safety:** The ring buffer uses atomics. Only the main thread runs signal handlers (POSIX semantics). In a future threaded runtime, a dedicated signal thread would drain the buffer and post events to the main scheduler.
- **Reentrancy:** The C handler only writes to the ring buffer (async-signal-safe). All Saffron-level handler execution happens outside signal context.

## Error Handling

| Condition | Behavior |
|-----------|----------|
| Invalid signal number | Throws `"signal: invalid signal number N"` |
| SIGKILL/SIGSTOP (uncatchable) | Throws `"signal: cannot trap SIGKILL/SIGSTOP"` |
| `_register` returns -1 (errno) | Throws `"signal: registration failed (errno E)"` |
| Handler throws | Exception propagates to the scheduler's unhandled-exception hook |

## Constants

Defined as module-level `var` bindings (no enum needed — signals are just integers):

```saffron
var SIGINT: Int  = 2
var SIGTERM: Int = 15
var SIGHUP: Int  = 1
var SIGUSR1: Int = 10
var SIGUSR2: Int = 12
var SIGPIPE: Int = 13
var SIGALRM: Int = 14
var SIGCHLD: Int = 17
```

Platform-specific values (Linux vs macOS differ for SIGUSR1/2) are resolved at compile time via `#ifdef` in the C shim, exposed through an `_sf_signal_value(name_tag)` extern if needed.

## Implementation Plan

1. **C shim** — `cvm/libc/signal_native.c`: ring buffer + sigaction wrapper (~80 LOC)
2. **Stdlib module** — `src/lib/signal.sf`: extern decls, handler map, dispatch loop, `on/trap/wait/ignore/reset`
3. **Scheduler hook** — add `_poll()` drain to the async scheduler tick (1 line in scheduler.sf)
4. **Build driver** — add `signal_native.c` to the clang link command in `tools/saffron`
5. **Tests** — `test/signal.sf`: trap SIGUSR1, send via `OS.exec("kill -USR1 $$")`, verify handler runs

## Open Questions

- Should `Signal.on` return a disposer function (for unregistering)?
- Should `Signal.wait` accept a timeout parameter (`Signal.wait(signals, 5.0)`)?
- Cross-platform: worth abstracting Windows `SetConsoleCtrlHandler` behind the same API?
