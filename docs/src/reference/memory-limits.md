# Memory Limits

Saffron can cap the heap a program is allowed to use. When an allocation would
push the program past the cap, the runtime attempts one garbage collection; if
the program is still over the limit afterwards it prints a diagnostic to stderr
and exits with status **3**.

This is useful for running untrusted or unbounded workloads (a script that builds
a string in a loop, a parser fed a hostile input) under a known ceiling instead of
letting the process grow until the OS kills it.

## Setting the limit

There are three ways to set the cap. They are listed in the order they take
effect, so a later one overrides an earlier one.

### 1. The `SAFFRON_MAX_MEMORY` environment variable

Read once at process startup, before `main` runs. This is the option that works
for a compiled binary:

```bash
saffron build report.sf -o report
SAFFRON_MAX_MEMORY=512m ./report
```

### 2. The `--max-memory` flag

Accepted by `saffron run`. It is a convenience wrapper that sets
`SAFFRON_MAX_MEMORY` for the program it launches:

```bash
saffron run --max-memory 512m report.sf
saffron run --max-memory=512m report.sf   # equals form also works
```

The flag applies to `saffron run` only. Passing it to `saffron build` is an error
rather than a silent no-op, because the cap is a property of *running* the binary,
not of compiling it — a built binary reads the environment variable instead.

### 3. `GC.set_max_memory()` at runtime

For a program that wants to impose its own ceiling, or raise one before a known
memory-hungry phase:

```saffron
import "@gc" as Memory

Memory.set_max_memory(512 * 1024 * 1024)   // 512 MiB
Memory.set_max_memory(0)                   // 0 disables the cap

IO.println(Memory.max_memory())            // current cap in bytes, 0 = unlimited
IO.println(Memory.live_bytes())            // bytes currently charged to the cap
```

> **Import it under a name other than `GC`.** Because of
> [BUGS #74](https://github.com/henry232323/saffron/blob/main/BUGS.md), calling
> these through an alias spelled exactly `GC` passes a NaN-boxed argument into the
> runtime and the cap is silently never installed. `import "@gc" as Memory` (or
> any other name) dispatches correctly. The `--max-memory` flag and the
> environment variable are not affected — only the Saffron-level setters are.

`live_bytes()` reports what the allocator says is *usable* for each outstanding
block, which runs slightly above the sum of the sizes you asked for. It is only
maintained while a cap is installed; with no cap it stays at `0`, so that
uncapped programs pay nothing for the accounting.

## Size syntax

A bare number is a byte count. An optional `k`, `m`, or `g` suffix multiplies by
1024, 1024², or 1024³ respectively, and is case-insensitive:

| Value | Bytes |
|---|---|
| `1048576` | 1048576 |
| `512k` / `512K` | 524288 |
| `64m` / `64M` | 67108864 |
| `2g` / `2G` | 2147483648 |

Anything else — `1.5m`, `12x`, `-1`, an empty string — is rejected. A malformed
`--max-memory` is caught by the driver before compilation starts; a malformed
`SAFFRON_MAX_MEMORY` is caught by the runtime at startup. Either way it is a
usage error and exits **1**, never a silent unlimited run.

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Normal completion; the cap was never reached. |
| `1` | The limit value itself was malformed. |
| `3` | The cap was breached, or an allocation genuinely failed. |

```
$ saffron run --max-memory 16m runaway.sf
saffron: out of memory: allocation exceeded --max-memory limit
$ echo $?
3
```

### Why these conventions

Both the `k`/`m`/`g` suffixes and exit code 3 deliberately follow the JVM. Java's
`-Xmx512m` established the suffix convention, and
`-XX:+ExitOnOutOfMemoryError` exits with status 3 on heap exhaustion. Matching it
means existing supervisor scripts, container health checks, and CI runners that
already special-case "the JVM ran out of memory" work unchanged against Saffron.

## The breach is fatal and cannot be caught

A cap breach is **not** a catchable Saffron exception. This `catch` never runs:

```saffron
try {
    var s = "x"
    for (i = 0; i < 30; i = i + 1) { s = s + s }
} catch (e) {
    IO.println("caught: ${e}")   // unreachable
}
```

The check fires inside the allocator, part-way through constructing an object.
Unwinding from there would leave a half-initialized value reachable from the
shadow stack, so the runtime reports and exits instead. This is stricter than an
ordinary runtime fault: index errors, division by zero and nil misuse ARE
catchable with `try`/`catch` (see [Error Handling](../tutorial/error-handling.md)),
but a cap breach is one of the few genuinely unrecoverable conditions — it always
exits, caught or not.

The error path itself allocates nothing: it writes a fixed message with a direct
`write(2, ...)`, because the normal diagnostic machinery builds its message on the
heap and would recurse forever under a hard cap.

## What the cap covers

On the native target the cap covers essentially all heap traffic. Every allocation
in the runtime and in generated code routes through a cap-aware wrapper rather
than calling `malloc` directly, which means string concatenation, closure
environments, enum payloads, coroutine frames, and the GC's own old-generation
allocations are all charged against it.

The cap tracks **live** bytes, not cumulative ones: a program that allocates and
frees steadily in a loop will not slowly trip a limit it never actually exceeded.

### Not enforced on wasm

`--max-memory`, `SAFFRON_MAX_MEMORY` and `GC.set_max_memory()` are **host-only**.
On `wasm32` and `wasm64` they are accepted and then ignored — the calls link
against inert stubs, `max_memory()` always reports `0`, and no limit is enforced.

Two things block it. The cap's enforcement code lives in `src/runtime/gc.ll`,
which is only linked for native builds. And the wasm allocator is a bump
allocator whose `free` is a no-op and which discards the result of
`memory.grow` — so there is no live-bytes figure to check a cap against, and no
out-of-memory condition it can currently detect at all. Capping wasm means fixing
the allocator first.
