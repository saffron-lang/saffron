# Turmeric

```saffron
import { signal, computed, effect } from "turmeric/signal"
import { cx } from "turmeric/style"
import { Event } from "turmeric/events"
import "turmeric/router" as Router
```

Turmeric is a reactive web framework for Saffron that compiles to WebAssembly. It uses fine-grained signal-based reactivity and inline HTML syntax to produce fast, small web apps with no virtual DOM.

## Overview

- **Inline HTML syntax** -- write `<div class="card">...</div>` directly in `.sf` files (JSX-like, recommended)
- **Signal reactivity** -- surgical DOM updates via `signal`, `computed`, and `effect`
- **Typed events** -- `Event` enum with `Click`, `Input`, `KeyDown`, etc.
- **All HTML elements** -- 112 typed element functions generated from the DOM spec
- **Client-side router** -- hash or history mode with route params
- **CSS utilities** -- `cx()` conditional classes, scoped styles, CSS variables
- **WASM target** -- compiles to < 40KB `.wasm` with a tiny JS loader

## Inline HTML Syntax (Recommended)

Turmeric supports JSX-like inline HTML directly in `.sf` files. No special file extension is needed -- the SFX transform handles it at compile time. This is the preferred way to write Turmeric apps:

```saffron
import { signal } from "turmeric/signal"
import { Event } from "turmeric/events"

fun Counter() {
    var count = signal(0)

    <div class="counter">
        <h1>Count: {count.get()}</h1>
        <button on_click={fun (e: Event) => count.update(fun (n) => n + 1)}>
            Increment
        </button>
    </div>
}

mount("#app", Counter)
```

### Syntax rules

| Feature | Syntax | Example |
|---------|--------|---------|
| Open/close tags | `<tag>...</tag>` | `<div>...</div>` |
| Attributes | `name="value"` | `<div class="card" id="main">` |
| Event handlers | `on_event={expr}` | `<button on_click={fun (e: Event) => ...}>` |
| Dynamic content | `{expression}` | `<span>{count.get()}</span>` |
| Self-closing tags | `<tag />` | `<input />`, `<br />`, `<img />` |
| Components | `{Component(args)}` | `{NavBar()}` |

### Conditional rendering

```saffron
<div>
    {if (is_logged_in.get()) {
        <p>Welcome back!</p>
    }}
</div>
```

### List rendering

```saffron
<ul>
    {var i: Float = 0
    while (i < items.length()) {
        <li>{items[i]}</li>
        i = i + 1
    }}
</ul>
```

### Attributes and events

Use standard HTML attribute names (`class`, `id`, `href`, etc.) and `on_*` for event handlers:

```saffron
<input
    class="px-4 py-2 border rounded"
    type="text"
    placeholder="Search..."
    on_input={fun (e: Event) => query.set(e.target_value)}
/>

<a class="text-blue-500" href="/about" on_click={fun (e: Event) => navigate("/about")}>
    About
</a>
```

### Component composition

Components are functions -- call them inside `{...}` blocks:

```saffron
fun App() {
    <div class="min-h-screen">
        {NavBar()}
        <main>
            {router.render()}
        </main>
        {Footer()}
    </div>
}
```

## Function Call Syntax (Alternative)

Turmeric also supports a builder DSL using trailing closures and named args. This was the original syntax and still works:

```saffron
fun Counter() {
    var count = signal(0)

    div(cls="counter") {
        h1 { reactive(fun () => "Count: " + count.get().to_string()) }
        button(cls="btn", on_click=fun (e: Event) => count.update(fun (n: Float): Float => n + 1)) {
            "Increment"
        }
    }
}

mount("#app", Counter)
```

Key differences from inline HTML: use `cls` instead of `class`, `type_` instead of `type`, string literals for static text, and `reactive()` wrappers for dynamic text.

## Quick Example

```saffron
import { signal, computed } from "turmeric/signal"
import { Event } from "turmeric/events"

var count = signal(0)
var doubled = computed(fun () => count.get() * 2)

fun App() {
    <div class="app">
        <h1>Count: {count.get()}</h1>
        <p>Doubled: {doubled.get()}</p>
        <button class="btn" on_click={fun (e: Event) => count.set(count.get() + 1)}>
            Increment
        </button>
    </div>
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

With inline HTML syntax, elements use standard HTML tag names and attributes:

```saffron
<div class="card" id="main">
    <h2>Title</h2>
    <p class="body">Content here</p>
    <a href="/about">Learn more</a>
</div>
```

Dynamic content uses `{expression}` interpolation:

```saffron
<h1>Hello, {name.get()}!</h1>
```

### Common attributes (inline HTML)

| Attribute | Notes |
|-----------|-------|
| `class` | CSS class |
| `id` | Element ID |
| `style` | Inline CSS string |
| `type` | Input type |
| `href`, `src`, `alt` | Standard HTML attrs |
| `placeholder`, `name`, `value` | Form input attrs |

### Common attributes (function call syntax)

| Attribute | Notes |
|-----------|-------|
| `cls` | CSS class (`class` is reserved in Saffron) |
| `id` | Element ID |
| `style` | Inline CSS string |
| `type_` | Input type (`type` is reserved) |
| `href`, `src`, `alt` | Standard HTML attrs |
| `placeholder`, `name`, `value` | Form input attrs |

## Events

Event handlers are passed as `on_*` attributes. The handler receives a typed `Event` enum:

```saffron
import { Event } from "turmeric/events"

<button on_click={fun (e: Event) => count.update(fun (n: Float): Float => n + 1)}>
    Click me
</button>

<input
    type="text"
    placeholder="Type here..."
    on_input={fun (e: Event) => query.set(e.target_value)}
/>
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

With inline HTML, dynamic content is handled by `{expression}` interpolation -- signals are automatically tracked:

```saffron
<div>
    <span>{count.get().to_string()} items</span>
</div>
```

### `reactive(fn)` (function call syntax)

In function call syntax, `reactive()` creates a `<span>` that re-renders its text when signals change:

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

<div class={cx({"card": true, "active": selected.get(), "disabled": !enabled.get()})}>
    Content
</div>
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

Turmeric apps commonly use Tailwind utility classes directly:

```saffron
<div class="flex items-center gap-4 p-6 bg-white rounded-lg shadow">
    <h2 class="text-xl font-bold text-gray-900">Card Title</h2>
    <p class="text-gray-600">Description</p>
</div>
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

Components are functions that build elements. Call them inside `{...}`:

```saffron
fun Card(title: String, body: String) {
    <div class="card">
        <h2>{title}</h2>
        <p>{body}</p>
    </div>
}

fun App() {
    <div>
        {Card("Hello", "This is a card.")}
        {Card("World", "Another card.")}
    </div>
}
```

## Mounting

Attach the app to a DOM element:

```saffron
fun App() {
    <div><h1>Hello World</h1></div>
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
