# Turmeric

```saffron
import { signal, computed, effect } from "turmeric/signal"
import { cx } from "turmeric/style"
import { Event } from "turmeric/events"
import "turmeric/router" as Router
```

Turmeric is a reactive web framework for Saffron that compiles to WebAssembly. It uses fine-grained signal-based reactivity and a builder DSL to produce fast, small web apps with no virtual DOM.

## Overview

- **Signal reactivity** -- surgical DOM updates via `signal`, `computed`, and `effect`
- **Builder syntax** -- `div(cls="card") { children }` using trailing closures and named args
- **Typed events** -- `Event` enum with `Click`, `Input`, `KeyDown`, etc.
- **All HTML elements** -- 112 typed element functions generated from the DOM spec
- **Client-side router** -- hash or history mode with route params
- **CSS utilities** -- `cx()` conditional classes, scoped styles, CSS variables
- **WASM target** -- compiles to < 40KB `.wasm` with a tiny JS loader

## Quick Example

```saffron
import { signal, computed } from "turmeric/signal"
import { Event } from "turmeric/events"

var count = signal(0)
var doubled = computed(fun () => count.get() * 2)

fun App() {
    div(cls="app") {
        h1 { reactive(fun () => "Count: " + count.get().to_string()) }
        p { reactive(fun () => "Doubled: " + doubled.get().to_string()) }
        button(cls="btn", on_click=fun (e: Event) => count.set(count.get() + 1)) {
            "Increment"
        }
    }
}

mount("#app", App)
```

## Reactivity

### `signal(initial)`

Creates a mutable reactive value. Call `.get()` to read (tracks dependency), `.set(value)` to write (notifies subscribers), `.update(fn)` for read-then-write.

```saffron
var name = signal("world")
name.get()                    // "world"
name.set("Saffron")           // notifies subscribers
name.update(fun (s: String): String => s.to_upper())
```

### `computed(fn)`

Derives a value from other signals. Recomputes automatically when dependencies change.

```saffron
var items = signal([1, 2, 3])
var total = computed(fun () => items.get().length())
```

### `effect(fn)`

Runs a side-effecting function immediately, then re-runs whenever its dependencies change.

```saffron
effect(fun () => {
    IO.println("Name is now: " + name.get())
})
```

### `batch(fn)`

Batches multiple signal updates, deferring notifications until the batch completes.

```saffron
batch(fun () => {
    first_name.set("Jane")
    last_name.set("Smith")
})
// Subscribers fire once with final state
```

### `untrack(fn)`

Reads signals inside `fn` without creating dependencies.

```saffron
effect(fun () => {
    var theme = untrack(fun () => config.get())  // not a dependency
    IO.println(count.get().to_string())          // this IS a dependency
})
```

## Elements

HTML elements are functions with named args for attributes and a trailing closure for children:

```saffron
div(cls="card", id="main") {
    h2 { "Title" }
    p(cls="body") { "Content here" }
    a(href="/about") { "Learn more" }
}
```

String literals inside trailing closures become text nodes. Use `reactive()` for dynamic text:

```saffron
h1 { reactive(fun () => "Hello, " + name.get() + "!") }
```

### Common attributes

| Attribute | Notes |
|-----------|-------|
| `cls` | CSS class (`class` is reserved) |
| `id` | Element ID |
| `style` | Inline CSS string |
| `type_` | Input type (`type` is reserved) |
| `href`, `src`, `alt` | Standard HTML attrs |
| `placeholder`, `name`, `value` | Form input attrs |

## Events

Event handlers are passed as `on_*` named arguments. The handler receives a typed `Event` enum:

```saffron
import { Event } from "turmeric/events"

button(on_click=fun (e: Event) => {
    count.update(fun (n: Float): Float => n + 1)
}) { "Click me" }

input(
    type_="text",
    placeholder="Type here...",
    on_input=fun (e: Event) => query.set("typed")
)
```

Available handlers: `on_click`, `on_dblclick`, `on_mousedown`, `on_mouseup`, `on_mousemove`, `on_mouseenter`, `on_mouseleave`, `on_keydown`, `on_keyup`, `on_input`, `on_change`, `on_submit`, `on_focus`, `on_blur`, `on_scroll`, `on_wheel`.

### Event data

The `Event` enum carries typed data for each event kind:

```saffron
match (e) {
    Click(data) => IO.println(data.client_x.to_string())
    KeyDown(data) => IO.println(data.key)
    Input(data) => name.set(data.value)
    _ => {}
}
```

## Reactive DOM Updates

### `reactive(fn)`

Creates a `<span>` that re-renders its text whenever the signals read inside `fn` change:

```saffron
div {
    reactive(fun () => count.get().to_string() + " items")
}
```

### `reactive_class(el, fn)`

Reactively updates an element's class name:

```saffron
var btn = button(cls="nav-link") { "Home" }
reactive_class(btn, fun () => {
    if (page.get() == "/") { return "nav-link active" }
    return "nav-link"
})
```

### `reactive_attr(el, name, fn)`

Reactively updates any attribute:

```saffron
var box = div(cls="box") { "..." }
reactive_attr(box, "style", fun () => "color: " + color.get())
```

## Styling

### Conditional classes with `cx()`

```saffron
import { cx } from "turmeric/style"

div(cls=cx({"card": true, "active": selected.get(), "disabled": !enabled.get()})) {
    "Content"
}
```

### Scoped styles

```saffron
import { scoped_styles } from "turmeric/style"

var s = scoped_styles({
    "container": "padding: 1rem; border: 1px solid #ddd;",
    "title": "font-size: 1.25rem; font-weight: bold;"
})
// s.get("container") returns a unique scoped class name
```

### Tailwind CSS

Turmeric apps commonly use Tailwind utility classes directly in `cls`:

```saffron
div(cls="flex items-center gap-4 p-6 bg-white rounded-lg shadow") {
    h2(cls="text-xl font-bold text-gray-900") { "Card Title" }
    p(cls="text-gray-600") { "Description" }
}
```

## Routing

```saffron
import "turmeric/router" as Router

var router = Router.hash()    // or Router.history() or Router.memory("/")

router.route("/", fun (p: Router.RouteParams) => HomePage())
router.route("/users/:id", fun (p: Router.RouteParams) => UserPage(p.get("id")))
router.route("*", fun (p: Router.RouteParams) => NotFound())

// Navigate programmatically
router.go("/users/42")
```

### Route patterns

| Pattern | Example match | Params |
|---------|--------------|--------|
| `/` | exact root | -- |
| `/users/:id` | `/users/42` | `{id: "42"}` |
| `/files/*path` | `/files/a/b.txt` | `{path: "a/b.txt"}` |
| `*` | anything | -- |

## Components

Components are functions that build elements:

```saffron
fun Card(title: String, body: String) {
    div(cls="card") {
        h2 { title }
        p { body }
    }
}

fun App() {
    div {
        Card("Hello", "This is a card.")
        Card("World", "Another card.")
    }
}
```

## Mounting

Attach the app to a DOM element:

```saffron
fun App() {
    div { h1 { "Hello World" } }
}

mount("#app", App)
```

## Building

Turmeric apps are built with `pantry`:

```bash
pantry new myapp --template=web
cd myapp
pantry build    # compiles to .wasm + generates HTML/JS loader
```

The output is a self-contained static site you can serve from any HTTP server.
