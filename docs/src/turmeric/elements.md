# Elements

Turmeric supports two syntaxes for building the DOM tree. **Inline HTML** (JSX-like) is the recommended approach -- it works directly in `.sf` files with no special file extension needed.

## Inline HTML Syntax (Recommended)

Write HTML directly in your Saffron code. The SFX transform compiles it at build time:

```saffron
<div class="container">
    <h1>Hello, Turmeric!</h1>
    <p>A reactive web framework for Saffron.</p>
</div>
```

Text inside tags becomes text nodes. Use `{expression}` for dynamic content:

```saffron
<h1>Hello, {name.get()}!</h1>
<span>{count.get()} items</span>
```

## Attributes

Standard HTML attribute names work directly:

```saffron
<div class="container" id="main">
    <a href="/about">About</a>
    <img src="/logo.png" alt="Logo" />
    <input type="text" placeholder="Search..." name="q" />
</div>
```

### Dynamic attributes

Use `{expression}` for computed attribute values:

```saffron
<div class={cx({"card": true, "active": is_selected.get()})}>
    Content
</div>
```

## Event Handlers

Attach event handlers with `on_*` attributes. The value is wrapped in `{...}`:

```saffron
import { Event } from "turmeric/events"

<button on_click={fun (e: Event) => count.update(fun (n) => n + 1)}>
    Click me
</button>

<input
    type="text"
    on_input={fun (e: Event) => name.set(e.target_value)}
/>

<form on_submit={fun (e: Event) => handle_submit()}>
    <button type="submit">Submit</button>
</form>
```

Available event handlers:
- **Mouse:** `on_click`, `on_dblclick`, `on_mousedown`, `on_mouseup`, `on_mousemove`, `on_mouseenter`, `on_mouseleave`
- **Keyboard:** `on_keydown`, `on_keyup`
- **Form:** `on_input`, `on_change`, `on_submit`, `on_focus`, `on_blur`
- **Scroll:** `on_scroll`, `on_wheel`

## Self-Closing Tags

Void elements use self-closing syntax:

```saffron
<br />
<hr />
<img src="/photo.jpg" alt="Photo" />
<input type="email" placeholder="you@example.com" />
```

## Dynamic Content with `{...}`

Embed any Saffron expression inside `{...}`:

```saffron
<div>
    <span>{user.get_name()}</span>
    <span>{items.length().to_string()} results</span>
</div>
```

### Conditional rendering

```saffron
<div>
    {if (is_loading.get()) {
        <div class="spinner">Loading...</div>
    }}
    {if (error.get().length() > 0) {
        <p class="error">{error.get()}</p>
    }}
</div>
```

### List rendering

```saffron
<ul class="todo-list">
    {var items = todos.get()
    var i: Float = 0
    while (i < items.length()) {
        <li class="todo-item">{items[i]}</li>
        i = i + 1
    }}
</ul>
```

### Components inside JSX

Components are functions -- call them inside `{...}`:

```saffron
fun App() {
    <div class="min-h-screen">
        {NavBar()}
        <main>
            {HomePage()}
        </main>
        {Footer()}
    </div>
}
```

## Available Elements

All 112 HTML elements are available, generated from the DOM spec:

**Layout:** `div`, `span`, `section`, `article`, `aside`, `header`, `footer`, `main`, `nav`

**Text:** `h1`-`h6`, `p`, `pre`, `code`, `blockquote`, `em`, `strong`, `small`, `mark`

**Lists:** `ul`, `ol`, `li`, `dl`, `dt`, `dd`

**Tables:** `table`, `thead`, `tbody`, `tfoot`, `tr`, `th`, `td`, `caption`

**Forms:** `form`, `input`, `textarea`, `select`, `option`, `button`, `label`, `fieldset`, `legend`

**Media:** `img`, `audio`, `video`, `canvas`, `source`, `picture`

**Void (self-closing):** `br`, `hr`, `img`, `input`, `link`, `meta`, `source`, `track`, `wbr`

## Real-World Example

From the Bazaar package registry frontend:

```saffron
fun PackageCard(pkg: Map<String, String>) {
    var name: String = pkg.get("name")
    var version: String = pkg.get("latest")

    <div class="bg-navy-800 border border-gray-700 rounded-lg p-5 hover:border-saffron-400 transition-all cursor-pointer" on_click={fun (e: Event) => load_package_detail(name)}>
        <div class="flex items-center justify-between mb-2">
            <h2 class="text-lg font-bold text-white">{name}</h2>
            <span class="text-xs font-mono bg-gray-700 text-saffron-400 px-2 py-0.5 rounded">
                v{version}
            </span>
        </div>
        <div class="font-mono text-xs bg-navy-950 text-gray-300 px-3 py-2 rounded border border-gray-700">
            <code>pantry add {name}</code>
        </div>
    </div>
}
```

## Function Call Syntax (Alternative)

The original builder DSL also works. Elements are functions with named args for attributes and trailing closures for children:

```saffron
div(cls="container", id="main") {
    a(href="/about") { "About" }
    img(src="/logo.png", alt="Logo")
    input(type_="text", placeholder="Search...", name="q")
}
```

Key differences from inline HTML:

| Feature | Inline HTML | Function call |
|---------|-------------|---------------|
| CSS class | `class="..."` | `cls="..."` |
| Input type | `type="..."` | `type_="..."` |
| Static text | Plain text inside tags | String literals in trailing closure |
| Dynamic text | `{expression}` | `reactive(fun () => ...)` |
| Event handlers | `on_click={expr}` | `on_click=expr` |
| Self-closing | `<img />` | `img(src="...")` (no closure) |

### Event handlers (function call syntax)

```saffron
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

### Reactive text (function call syntax)

Use `reactive()` to create text that updates when signals change:

```saffron
div {
    reactive(fun () => "Count: " + count.get().to_string())
}
```

### Components (function call syntax)

```saffron
fun Card(title: String, body: String) {
    div(cls="card") {
        h2 { title }
        p { body }
    }
}

div {
    Card("Hello", "This is a card component")
    Card("Another", "More content here")
}
```

## Mounting

Use `mount()` to attach the app to a DOM element:

```saffron
fun App() {
    <div><h1>Hello World</h1></div>
}

mount("#app", App)
```
