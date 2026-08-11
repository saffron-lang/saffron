# Reactivity

Sumac state is reactive. You hold values in **signals**, derive from them with
**computed** values, and run side effects with **effects**. This is the same
model Turmeric uses on the web — Sumac ships its own `signal.sf` with an
identical API, so if you know Turmeric's signals, you know Sumac's. Everything on
this page is from the **built** `signal.sf`.

```saffron
import { signal, computed, effect, batch, untrack } from "sumac/signal"
```

## Signal

A signal holds a mutable value and notifies subscribers when it changes.

```saffron
var count = signal(0)

count.get()      // 0                        (read; tracked)
count.set(5)     // notifies all subscribers
count.update(fun (n) => n + 1)   // read + write in one step
```

`count.get()` records a dependency when read inside a tracked context (a
`computed`, an `effect`, or the runtime's render pass) — that is how Sumac knows
to re-render when the signal changes. `count.update(fn)` is sugar for
`set(fn(get()))`.

### Subscribe

You can subscribe manually, but in practice you'll use `effect` instead:

```saffron
count.subscribe(fun () => {
    IO.println("count is now ${count.get()}")
})
```

## Computed

A computed value derives from other signals and re-evaluates when its
dependencies change:

```saffron
var first = signal("Ada")
var last = signal("Lovelace")

var full = computed(fun () => first.get() + " " + last.get())

full.get()       // "Ada Lovelace"
first.set("Grace")
full.get()       // "Grace Lovelace"
```

Computed values can depend on other computed values, forming a graph that
updates consistently.

## Effect

An effect runs immediately, then re-runs whenever any signal it read changes.
Dependencies are tracked automatically — you never declare them.

```saffron
var name = signal("world")

effect(fun () => {
    IO.println("Hello, ${name.get()}!")
})
// prints: Hello, world!

name.set("Sumac")
// prints: Hello, Sumac!
```

`effect` returns an `EffectHandle`; call `handle.dispose()` to unsubscribe it.

### How this drives rendering

The runtime renders inside a tracked context. Your `view()` reads signals via
`.get()`; those reads subscribe the render to the signals. When any of them
changes, the frame is marked dirty, `view()` re-runs, the back buffer is
repainted, and the diff flushes only the changed cells. You write no redraw code
— mutating a signal *is* the redraw request.

```saffron
var count = signal(0)

fun view() {
    text("count: ${count.get()}")   // read subscribes the render
}

on_key("+", fun () => count.set(count.get() + 1))   // triggers a repaint
```

## Batch

Batch multiple updates so subscribers fire once, after all changes are applied:

```saffron
var first = signal("Ada")
var last = signal("Lovelace")

batch(fun () => {
    first.set("Grace")
    last.set("Hopper")
})
// dependents see the final state and fire a single time
```

## Untrack

Read a signal without creating a dependency:

```saffron
var config = signal("dark")
var count = signal(0)

effect(fun () => {
    // Re-runs when `count` changes, but NOT when `config` changes.
    var theme = untrack(fun () => config.get())
    IO.println("count ${count.get()}, theme ${theme}")
})
```

## Owning widget state

Sumac widgets don't hide their state — you create the signals and hand them to
the widget's constructor, so app code stays the source of truth:

```saffron
var selected = signal(0)
var lw = ListWidget(items, selected)   // widget binds to your signal

// in view():   lw.view()
// runtime feeds input:  lw.handle(event)
```

Because `selected` is your signal, you can read it, react to it with a
`computed`/`effect`, or set it from a key handler — the widget and the rest of
your UI stay in sync automatically. See [Widgets](./widgets.md).
</content>
