# Elements

Turmeric uses builder functions with trailing closures to construct a virtual element tree. Every HTML element has a corresponding Saffron function.

## Basic Usage

```saffron
div {
    h1 { text("Hello, Turmeric!") }
    p { text("A reactive web framework.") }
}
```

The `text()` function creates a text node inside the current element.

## Attributes

Pass attributes as the first argument — a list of `Attr` values:

```saffron
div([class_("container"), id("main")]) {
    a([href("/about")]) { text("About") }
    img([src("/logo.png"), alt("Logo")])
    input([type_("text"), placeholder("Search..."), name("q")])
}
```

When no attributes are needed, just use the trailing closure directly:

```saffron
ul {
    li { text("First") }
    li { text("Second") }
    li { text("Third") }
}
```

## Available Elements

All 112 HTML elements are available as functions, generated from the TypeScript DOM spec:

**Layout:** `div`, `span`, `section`, `article`, `aside`, `header`, `footer`, `main`, `nav`

**Text:** `h1`–`h6`, `p`, `pre`, `code`, `blockquote`, `em`, `strong`, `small`, `mark`

**Lists:** `ul`, `ol`, `li`, `dl`, `dt`, `dd`

**Tables:** `table`, `thead`, `tbody`, `tfoot`, `tr`, `th`, `td`, `caption`

**Forms:** `form`, `input`, `textarea`, `select`, `option`, `button`, `label`, `fieldset`, `legend`

**Media:** `img`, `audio`, `video`, `canvas`, `source`, `picture`

**Void (self-closing):** `br`, `hr`, `img`, `input`, `link`, `meta`, `source`, `track`, `wbr`

## Attribute Helpers

Common attributes have typed helper functions:

```saffron
class_("btn primary")       // class="btn primary"
id("submit-btn")            // id="submit-btn"
href("/page")               // href="/page"
src("/img.png")             // src="/img.png"
alt("description")          // alt="description"
type_("submit")             // type="submit"
name("email")               // name="email"
value("hello")              // value="hello"
placeholder("Type here")    // placeholder="Type here"
disabled("true")            // disabled="true"
style("color: red")         // style="color: red"
```

Note: `class_`, `type_`, `for_`, `readonly_` use trailing underscores to avoid Saffron keyword conflicts.

### Data and ARIA Attributes

```saffron
data("id", "42")            // data-id="42"
aria("label", "Close")      // aria-label="Close"
```

## Event Handlers

Event handlers are typed — each event gets the correct event class:

```saffron
button([on_click(fun (e: PointerEvent) => {
    IO.println("Clicked at ${e.client_x}, ${e.client_y}")
})]) {
    text("Click me")
}

input([on_input(fun (e: InputEvent) => {
    IO.println("Input: ${e.data}")
}), on_keydown(fun (e: KeyboardEvent) => {
    if (e.key == "Enter") { submit() }
})])
```

Available event handlers include:
- **Mouse:** `on_click`, `on_dblclick`, `on_mousedown`, `on_mouseup`, `on_mousemove`, `on_mouseenter`, `on_mouseleave`
- **Keyboard:** `on_keydown`, `on_keyup`
- **Form:** `on_input`, `on_change`, `on_submit`, `on_reset`, `on_focus`, `on_blur`
- **Pointer:** `on_pointerdown`, `on_pointerup`, `on_pointermove`
- **Drag:** `on_dragstart`, `on_drag`, `on_dragend`, `on_drop`
- **Touch:** `on_touchstart`, `on_touchmove`, `on_touchend`
- **Animation:** `on_animationstart`, `on_animationend`, `on_transitionend`

## Components

Components are just functions that return `Node`:

```saffron
fun Card(title: String, body: String): Node {
    return div([class_("card")]) {
        h2 { text(title) }
        p { text(body) }
    }
}

// Use like any other element
div {
    Card("Hello", "This is a card component")
    Card("Another", "More content here")
}
```

## Conditional Rendering

Use `show()` or pattern matching:

```saffron
var logged_in = signal(true)

div {
    show(logged_in.get()) {
        p { text("Welcome back!") }
    }

    // Or with match:
    match (user.get()) {
        Loading => p { text("Loading...") }
        is Ready(u) => h1 { text(u.name) }
        is Error(msg) => p([class_("error")]) { text(msg) }
    }
}
```

## List Rendering

Use `each()` with a trailing closure that receives each item:

```saffron
var todos = signal(["Buy milk", "Write code", "Ship it"])

ul {
    each(todos.get()) { todo =>
        li { text(todo) }
    }
}
```

## Signal Binding

Bind a signal to an input's value for two-way data flow:

```saffron
var query = signal("")

input([bind_value(query), placeholder("Search...")])
p { text("You typed: ${query.get()}") }
```

## Rendering to HTML

For testing or server-side rendering:

```saffron
var tree = div { h1 { text("Hello") } }
var html = render_to_html(tree)
IO.println(html)  // <div><h1>Hello</h1></div>
```
