# Signals

Turmeric's reactivity system is built on three primitives: **signals**, **computed values**, and **effects**.

```saffron
import { signal, computed, effect } from "turmeric/signal"
```

## Signal

A signal holds a mutable value and notifies subscribers when it changes.

```saffron
var count = signal(0)

// Read
count.get()     // 0

// Write
count.set(5)    // notifies all subscribers

// Update (read + write)
count.update(fun (n: Int): Int => n + 1)
```

### Subscribe

Manually subscribe to a signal's changes:

```saffron
count.subscribe(fun () => {
    IO.println("count changed to: ${count.get()}")
})
```

In practice you'll rarely use `subscribe` directly — use `effect` instead.

## Computed

A computed value derives from other signals. It re-evaluates automatically when its dependencies change.

```saffron
var firstName = signal("John")
var lastName = signal("Doe")

var fullName = computed(fun () => firstName.get() + " " + lastName.get())

fullName.get()  // "John Doe"

firstName.set("Jane")
fullName.get()  // "Jane Doe"
```

Computed values are lazy — they only recompute when read after a dependency changes.

### Chaining

Computed values can depend on other computed values:

```saffron
var items = signal([1, 2, 3, 4, 5])
var even = computed(fun () => items.get().filter(fun (n: Int): Bool => n % 2 == 0))
var count = computed(fun () => even.get().length())

count.get()  // 2
items.set([2, 4, 6, 8])
count.get()  // 4
```

## Effect

An effect runs a side-effecting function immediately, then re-runs it whenever any signal it read changes.

```saffron
var name = signal("world")

effect(fun () => {
    IO.println("Hello, ${name.get()}!")
})
// Prints: Hello, world!

name.set("Turmeric")
// Prints: Hello, Turmeric!
```

### Automatic Dependency Tracking

You don't declare dependencies — they're tracked automatically:

```saffron
var a = signal(1)
var b = signal(2)
var c = signal(3)

effect(fun () => {
    // Only reads `a` and `b` — NOT subscribed to `c`
    IO.println((a.get() + b.get()).to_string())
})

c.set(100)  // Effect does NOT re-run
a.set(10)   // Effect re-runs, prints "12"
```

## Batch

Batch multiple signal updates to defer notifications until all changes are applied:

```saffron
var first = signal("John")
var last = signal("Doe")

// Without batch: effect fires twice
// With batch: effect fires once with final state
batch(fun () => {
    first.set("Jane")
    last.set("Smith")
})
```

## Untrack

Read a signal without creating a dependency:

```saffron
var config = signal("dark")
var count = signal(0)

effect(fun () => {
    // This effect re-runs when `count` changes, but NOT when `config` changes
    var theme = untrack(fun () => config.get())
    IO.println("Count: ${count.get()}, theme: ${theme}")
})
```
