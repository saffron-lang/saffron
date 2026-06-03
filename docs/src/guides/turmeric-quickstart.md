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

Edit `src/main.sf`:

```saffron
import { signal, computed } from "turmeric/signal"
import { Event } from "turmeric/events"

var count = signal(0)
var doubled = computed(fun () => count.get() * 2)

fun App() {
    div(cls="container") {
        h1 { "My First Turmeric App" }

        div(cls="counter") {
            p { reactive(fun () => "Count: " + count.get().to_string()) }
            p { reactive(fun () => "Doubled: " + doubled.get().to_string()) }

            button(on_click=fun (e: Event) => count.update(fun (n: Float): Float => n + 1)) {
                "+1"
            }
            button(on_click=fun (e: Event) => count.set(0)) {
                "Reset"
            }
        }
    }
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

1. **Signals** hold reactive state. Calling `.get()` inside `reactive()` or `effect()` creates a subscription.
2. **Element functions** (`div`, `h1`, `button`, etc.) create real DOM nodes via the JS runtime bridge.
3. **Trailing closures** define children. String literals become text nodes.
4. **Named args** map to HTML attributes and event handlers (`cls`, `on_click`, `type_`, etc.).
5. **`mount("#app", App)`** finds the `#app` element in `index.html` and runs your `App` function to build the DOM tree.

## Add a todo list

Expand the app with list state:

```saffron
var todos = signal(["Buy milk", "Write code"])
var new_todo = signal("")

fun TodoApp() {
    div(cls="todo-app") {
        h2 { "Todos" }
        p { reactive(fun () => todos.get().length().to_string() + " items") }

        div(cls="input-row") {
            input(
                type_="text",
                placeholder="Add a task...",
                on_input=fun (e: Event) => new_todo.set("task")
            )
            button(on_click=fun (e: Event) => {
                todos.update(fun (list: List<String>): List<String> => {
                    list.push("New task")
                    return list
                })
            }) { "Add" }
        }

        ul {
            var items = todos.get()
            var i = 0
            while (i < items.length()) {
                li { items[i] }
                i = i + 1
            }
        }
    }
}
```

## Add routing

```saffron
import "turmeric/router" as Router

var router = Router.hash()

router.route("/", fun (p: Router.RouteParams) => HomePage())
router.route("/about", fun (p: Router.RouteParams) => AboutPage())

fun NavBar() {
    nav {
        button(on_click=fun (e: Event) => router.go("/")) { "Home" }
        button(on_click=fun (e: Event) => router.go("/about")) { "About" }
    }
}

fun HomePage() {
    section { h1 { "Home" } }
}

fun AboutPage() {
    section { h1 { "About" } }
}

fun App() {
    div {
        NavBar()
        main {
            // Route dispatch
            var path = router.current_signal().get()
            if (path == "/about") { AboutPage() }
            else { HomePage() }
        }
    }
}

mount("#app", App)
```

## Next steps

- [Signals](../turmeric/signals.md) -- full reactivity API
- [Elements](../turmeric/elements.md) -- all element functions and attributes
- [Router](../turmeric/router.md) -- route patterns, params, and navigation
- [Styling](../turmeric/styling.md) -- CSS approaches (Tailwind, scoped styles, `cx()`)
- [Component Lifecycle](../turmeric/lifecycle.md) -- mount, update, unmount, and cleanup
