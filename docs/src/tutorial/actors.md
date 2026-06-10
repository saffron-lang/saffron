# Actors

Actors provide safe concurrency by isolating mutable state. Each actor processes one message at a time — no data races, no locks.

## Defining an actor

An actor is like a class, but with serialized access to its methods:

```saffron
actor Counter {
    var count: Int

    fun init() {
        this.count = 0
    }

    fun increment() {
        this.count = this.count + 1
    }

    fun get(): Int {
        return this.count
    }
}
```

## Using actors

Call actor methods just like class methods. From non-async code, they execute immediately:

```saffron
var c = Counter()
c.increment()
c.increment()
IO.println(c.get().to_string())  // 2
```

## Concurrent access

When multiple tasks call the same actor, their calls are serialized automatically:

```saffron
import "@async" as Async

actor BankAccount {
    var balance: Int
    fun init(amount: Int) { this.balance = amount }
    fun deposit(n: Int) { this.balance = this.balance + n }
    fun get_balance(): Int { return this.balance }
}

var account = BankAccount(100)

var t1 = Task.spawn(fun () => account.deposit(50))
var t2 = Task.spawn(fun () => account.deposit(30))
t1.await()
t2.await()

// Always 180, regardless of scheduling order
IO.println(account.get_balance().to_string())
```

If the actor is busy processing one call, other callers suspend and retry when it becomes idle.

## Self-calls

Methods can call other methods on `this` without deadlocking:

```saffron
actor Calculator {
    var result: Int
    fun init() { this.result = 0 }

    fun compute(n: Int): Int {
        this.result = this.square(n)  // direct call, no suspend
        return this.result
    }

    fun square(n: Int): Int {
        return n * n
    }
}
```

Self-calls bypass the busy check — they execute synchronously within the actor.

## Fire-and-forget with `send`

Use `.send(method, args)` to enqueue a message without waiting for a response:

```saffron
actor Logger {
    var entries: List<String>
    fun init() { this.entries = [] }
    fun log(msg: String) { this.entries.push(msg) }
    fun count(): Int { return this.entries.length() }
}

var logger = Logger()
logger.send("log", "request started")   // returns immediately
logger.send("log", "request finished")  // doesn't wait for first to complete

// Later, the messages are processed in order
```

`send` is useful for logging, events, metrics, and any case where you don't need the return value.

## Sendable types

Actor methods enforce that their parameters and return values are **Sendable** — safe to pass across task boundaries:

| Type | Sendable? |
|------|-----------|
| `Int`, `Float`, `Bool`, `String`, `Nil` | Yes |
| Enum types | Yes |
| Other actors | Yes |
| Classes extending `Sendable` | Yes |
| `List`, `Map` | No (mutable containers) |
| Closures | No (may capture mutable state) |
| Mutable classes | No |

The compiler warns when a non-Sendable type crosses an actor boundary:

```saffron
class Buffer {
    var data: List<String>
    fun init() { this.data = [] }
}

actor Worker {
    fun process(buf: Buffer) {}
    // Warning: parameter 'buf' has non-Sendable type 'Buffer'
}
```

### Making a class Sendable

Mark a class with `extends Sendable` to opt in:

```saffron
class Point extends Sendable {
    var x: Int
    var y: Int
    fun init(x: Int, y: Int) { this.x = x; this.y = y }
}

actor Canvas {
    fun draw_at(p: Point) { ... }  // OK: Point is Sendable
}
```

### Task.spawn capture checks

The compiler also warns when a `Task.spawn` closure captures a non-Sendable variable that an actor also holds:

```saffron
var shared_list: List<String> = []

actor Worker {
    fun run() { ... }
}

// Warning: closure captures non-Sendable 'shared_list'
var t = Task.spawn(fun () => shared_list.push("data"))
```

## WASM target

On WASM targets, actors compile as regular synchronous classes — the single-threaded browser environment guarantees serialization naturally.

## How it works

Under the hood, actors use the same cooperative scheduler as `Task.spawn`:

1. Each actor instance has a hidden `__actor_busy` field
2. When a caller (inside a coroutine) sees the actor is busy, it suspends with yield reason 5
3. When the actor method completes, it wakes all waiting callers
4. Non-coroutine callers execute synchronously (no concurrency possible without the scheduler)
