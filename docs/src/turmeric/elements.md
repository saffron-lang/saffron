# Elements

Turmeric uses builder functions with trailing closures to construct the DOM tree. Every HTML element has a corresponding Saffron function. Attributes are passed as named arguments.

## Basic Usage

```saffron
div {
    h1 { "Hello, Turmeric!" }
    p { "A reactive web framework." }
}
```

String literals inside trailing closures are automatically desugared to text nodes.

## Attributes (Named Args)

Pass attributes as named arguments to element functions:

```saffron
div(cls="container", id="main") {
    a(href="/about") { "About" }
    img(src="/logo.png", alt="Logo")
    input(type_="text", placeholder="Search...", name="q")
}
```

When no attributes are needed, just use the trailing closure directly:

```saffron
ul {
    li { "First" }
    li { "Second" }
    li { "Third" }
}
```

## Available Elements

All 112 HTML elements are available as functions, generated from the TypeScript DOM spec:

**Layout:** `div`, `span`, `section`, `article`, `aside`, `header`, `footer`, `main`, `nav`

**Text:** `h1`-`h6`, `p`, `pre`, `code`, `blockquote`, `em`, `strong`, `small`, `mark`

**Lists:** `ul`, `ol`, `li`, `dl`, `dt`, `dd`

**Tables:** `table`, `thead`, `tbody`, `tfoot`, `tr`, `th`, `td`, `caption`

**Forms:** `form`, `input`, `textarea`, `select`, `option`, `button`, `label`, `fieldset`, `legend`

**Media:** `img`, `audio`, `video`, `canvas`, `source`, `picture`

**Void (self-closing):** `br`, `hr`, `img`, `input`, `link`, `meta`, `source`, `track`, `wbr`

## Common Attributes

| Attribute | Notes |
|-----------|-------|
| `cls` | Sets the CSS class (maps to `class` attribute) |
| `id` | Element ID |
| `style` | Inline CSS |
| `type_` | Trailing underscore to avoid keyword conflict |
| `href`, `src`, `alt` | Standard HTML attributes |
| `placeholder`, `name`, `value` | Form input attributes |
| `rows` | Textarea rows |

Note: `cls` is used instead of `class`, and `type_` instead of `type`, to avoid Saffron keyword conflicts.

## Event Handlers

Event handlers are passed as named `on_*` attributes with typed `Event` objects:

```saffron
import { Event } from "turmeric/events"

button(cls="btn", on_click=fun (e: Event) => {
    IO.println("Button clicked!")
    count.set(count.get() + 1)
}) {
    "Click me"
}

input(
    type_="text",
    on_input=fun (e: Event) => name.set("typed")
)
```

Available event handlers:
- **Mouse:** `on_click`, `on_dblclick`, `on_mousedown`, `on_mouseup`, `on_mousemove`, `on_mouseenter`, `on_mouseleave`
- **Keyboard:** `on_keydown`, `on_keyup`
- **Form:** `on_input`, `on_change`, `on_submit`, `on_focus`, `on_blur`

## Reactive Text

Use `reactive()` to create text that updates when signals change:

```saffron
div {
    reactive(fun () => "Count: " + count.get().to_string())
}
```

The function is re-evaluated whenever any signal read inside it changes, and the DOM text node is updated automatically.

## Conditional Classes with `cx()`

The `cx()` utility generates class strings from a map of class names to boolean conditions:

```saffron
import { cx } from "turmeric/style"

div(cls=cx({"card": true, "active": is_selected.get(), "disabled": !enabled.get()})) {
    "Content"
}
```

## Components

Components are just functions that build elements:

```saffron
fun Card(title: String, body: String) {
    div(cls="card") {
        h2 { title }
        p { body }
    }
}

// Use like any other element
div {
    Card("Hello", "This is a card component")
    Card("Another", "More content here")
}
```

## List Rendering with `each()`

Use `each()` with a trailing closure to render lists reactively:

```saffron
import { each } from "turmeric/reconcile"

var todos = signal(["Buy milk", "Write code", "Ship it"])

ul(cls="todo-list") {
    each(todos.get()) { todo =>
        li(cls="todo-item") {
            span { todo }
            button(cls="delete", on_click=fun (e: Event) => {
                // remove todo
            }) { "x" }
        }
    }
}
```

## Pattern Matching for Pages

Use `match` on signals for page routing:

```saffron
var page = signal("home")

main {
    match (page.get()) {
        "home" => HomePage()
        "about" => AboutPage()
        _ => section { p { "404 - Not Found" } }
    }
}
```

## Mounting

Use `mount()` to attach the app to a DOM element:

```saffron
fun App() {
    div { h1 { "Hello World" } }
}

mount("#app", App)
```
