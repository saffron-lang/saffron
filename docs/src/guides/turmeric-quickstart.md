# Turmeric Quickstart

This guide walks you through creating and running your first Turmeric web app.

## Prerequisites

- Saffron installed (see [Installation](../getting-started/installation.md))
- LLVM toolchain (for WASM compilation)
- A modern browser

## Create a project

```bash
pantry new myapp --template=web
cd myapp
```

This generates:

```
myapp/
  pantry.toml       # project manifest
  src/
    main.sf         # app entry point
  public/
    index.html      # HTML shell
    style.css       # optional styles
```

## Write your app

Edit `src/main.sf`. Turmeric uses JSX-like inline HTML directly in `.sf` files -- no special extension needed:

```saffron
import { signal, computed } from "turmeric/signal"
import { Event } from "turmeric/events"

var count = signal(0)
var doubled = computed(fun () => count.get() * 2)

fun App() {
    <div class="container">
        <h1>My First Turmeric App</h1>

        <div class="counter">
            <p>Count: {count.get()}</p>
            <p>Doubled: {doubled.get()}</p>

            <button on_click={fun (e: Event) => count.update(fun (n: Float): Float => n + 1)}>
                +1
            </button>
            <button on_click={fun (e: Event) => count.set(0)}>
                Reset
            </button>
        </div>
    </div>
}

mount("#app", App)
```

## Build

```bash
pantry build
```

This compiles your Saffron source to LLVM IR targeting `wasm32`, links the Turmeric runtime, and produces:

```
build/
  myapp.wasm        # compiled WebAssembly binary
  index.html        # HTML shell (copies from public/)
  loader.js         # tiny JS glue for DOM operations
  style.css         # copied from public/
```

## Run

Serve the `build/` directory with any static file server:

```bash
python3 -m http.server -d build 8080
# or
npx serve build
```

Open `http://localhost:8080` in your browser.

## How it works

1. **Inline HTML** -- `<div>`, `<button>`, etc. are compiled by the SFX transform into DOM creation calls. Works in any `.sf` file.
2. **`{expression}`** -- curly braces inside elements evaluate Saffron expressions. Signals read here are automatically tracked.
3. **Signals** hold reactive state. When a signal changes, any DOM that depends on it updates surgically.
4. **Event handlers** -- `on_click={...}`, `on_input={...}` attach typed event listeners.
5. **`mount("#app", App)`** finds the `#app` element in `index.html` and runs your `App` function to build the DOM tree.

## Add a todo list

Expand the app with list state:

```saffron
var todos = signal(["Buy milk", "Write code"])
var new_todo = signal("")

fun TodoApp() {
    <div class="todo-app">
        <h2>Todos</h2>
        <p>{todos.get().length().to_string()} items</p>

        <div class="input-row">
            <input
                type="text"
                placeholder="Add a task..."
                on_input={fun (e: Event) => new_todo.set(e.target_value)}
            />
            <button on_click={fun (e: Event) => {
                todos.update(fun (list: List<String>): List<String> => {
                    list.push(new_todo.get())
                    return list
                })
                new_todo.set("")
            }}>Add</button>
        </div>

        <ul>
            {var items = todos.get()
            var i: Float = 0
            while (i < items.length()) {
                <li>{items[i]}</li>
                i = i + 1
            }}
        </ul>
    </div>
}
```

## Add routing

```saffron
import "turmeric/router" as Router
import { Event } from "turmeric/events"

var router = Router.hash()

router.route("/", fun (p: Router.RouteParams) => HomePage())
router.route("/about", fun (p: Router.RouteParams) => AboutPage())

fun NavBar() {
    <nav class="flex gap-4 p-4 border-b">
        <button on_click={fun (e: Event) => router.go("/")}>Home</button>
        <button on_click={fun (e: Event) => router.go("/about")}>About</button>
    </nav>
}

fun HomePage() {
    <section><h1>Home</h1></section>
}

fun AboutPage() {
    <section><h1>About</h1></section>
}

fun App() {
    <div>
        {NavBar()}
        <main>
            {router.render()}
        </main>
    </div>
}

mount("#app", App)
```

## Next steps

- [Signals](../turmeric/signals.md) -- full reactivity API
- [Elements](../turmeric/elements.md) -- all element functions and attributes
- [Router](../turmeric/router.md) -- route patterns, params, and navigation
- [Styling](../turmeric/styling.md) -- CSS approaches (Tailwind, scoped styles, `cx()`)
- [Component Lifecycle](../turmeric/lifecycle.md) -- mount, update, unmount, and cleanup
