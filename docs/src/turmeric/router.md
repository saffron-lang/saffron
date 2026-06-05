# Router

Turmeric includes a client-side router for single-page applications. It maps URL paths to components using pattern matching with three routing modes.

## Setup

```saffron
import "turmeric/router" as Router
import { Link } from "turmeric/router"

var router = Router.hash()   // hash-based: /#/path
// or: Router.history()      // clean URLs: /path (needs server fallback)
// or: Router.memory("/")    // in-memory (testing/embedded)
```

## Defining Routes

```saffron
router.route("/", fun (p: Router.RouteParams) => HomePage())
router.route("/about", fun (p: Router.RouteParams) => AboutPage())
router.route("/users/:id", fun (p: Router.RouteParams) => {
    UserPage(p.get("id"))
})
```

## Rendering

Call `router.render()` inside your app layout where the routed content should appear:

```saffron
fun App() {
    <div cls="app">
        {NavBar()}
        <main>
            {router.render()}
        </main>
        {Footer()}
    </div>
}

mount("#app", fun () => App())
```

## Route Patterns

| Pattern | Matches | Params |
|---------|---------|--------|
| `/` | Exact root | — |
| `/about` | Exact path | — |
| `/users/:id` | Dynamic segment | `p.get("id")` → `"42"` |
| `/posts/:id/comments/:cid` | Multiple params | `p.get("id")`, `p.get("cid")` |

## Navigation Links

`Link` renders a real `<a href="...">` with SPA click interception. Right-click and ctrl+click open in new tab as expected.

```saffron
import { Link } from "turmeric/router"

fun NavBar() {
    <nav>
        Link(router, "/", cls="nav-link") {
            text("Home")
        }
        Link(router, "/about", cls="nav-link") {
            text("About")
        }
        Link(router, "/users/42", cls="nav-link") {
            text("User 42")
        }
    </nav>
}
```

The `href` format adapts to the router mode:
- **Hash**: `href="#/about"` — works without server config
- **History**: `href="/about"` — clean URLs, needs server to serve index.html for all paths
- **Memory**: `href="javascript:void(0)"` — no real navigation

### Link Parameters

```saffron
Link(router, to, cls="", id="", style="", block=nil)
```

| Param | Type | Description |
|-------|------|-------------|
| `router` | `Router` | The router instance |
| `to` | `String` | Target path (e.g., `"/about"`) |
| `cls` | `String` | CSS classes |
| `id` | `String` | Element ID |
| `style` | `String` | Inline style |
| `block` | `() => Nil` | Children (text, nested elements) |

## Programmatic Navigation

```saffron
router.go("/users/42")
router.go("/search?q=hello")
```

## Route Parameters

Route handlers receive `RouteParams`:

```saffron
class RouteParams {
    var path: String
    var params: Map<String, String>
    var query: Map<String, String>

    fun get(name: String): String  // shorthand for params.get(name)
}
```

```saffron
router.route("/users/:id", fun (p: Router.RouteParams) => {
    var user_id: String = p.get("id")
    <div>
        <h1>User {user_id}</h1>
    </div>
})
```

## Current Route Signal

The router's current path is a signal — use it for reactive UI (e.g., active nav styling):

```saffron
var current_path = router.current_signal()

// React to route changes
effect(fun () => {
    IO.println("Now at: " + current_path.get())
})
```

## Routing Modes

### Hash (`Router.hash()`)

Uses `location.hash` (`/#/path`). No server configuration needed — works with any static file server.

### History (`Router.history()`)

Uses `history.pushState` for clean URLs (`/path`). Requires the server to serve `index.html` for all paths (SPA fallback).

### Memory (`Router.memory("/")`)

Routes exist only in memory. Useful for testing or embedding a routed app inside another app.

## Implementation Notes

The router uses browser APIs via Saffron's FFI:
- Hash mode: reads/writes `location.hash`, listens to `hashchange`
- History mode: calls `history.pushState`, listens to `popstate`
- Route matching uses path segment comparison with `:param` extraction
- `render()` uses turmeric's `render_into` to clear and rebuild the outlet on navigation
