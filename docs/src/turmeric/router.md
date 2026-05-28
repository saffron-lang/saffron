# Router

Turmeric includes a client-side router for single-page applications. It maps URL paths to components using pattern matching.

## Basic Usage

```saffron
import "turmeric" as T
import "turmeric/router" as Router

fun App(): Node {
    return Router.create([
        Route("/", HomePage),
        Route("/about", AboutPage),
        Route("/users/:id", UserPage),
        Route("*", NotFoundPage),
    ])
}
```

## Route Patterns

| Pattern | Matches | Params |
|---------|---------|--------|
| `/` | Exact root | — |
| `/about` | Exact path | — |
| `/users/:id` | Dynamic segment | `{id: "42"}` |
| `/posts/:id/comments/:cid` | Multiple params | `{id: "5", cid: "3"}` |
| `/files/*path` | Catch-all (rest) | `{path: "docs/intro.md"}` |
| `*` | Wildcard (404) | — |

## Defining Routes

```saffron
enum Route {
    Route(path: String, component: (RouteParams) => Node)
}

class RouteParams {
    var params: Map<String, String>
    var query: Map<String, String>
    var path: String

    fun get(name: String): String {
        return this.params.get(name)
    }
}
```

## Route Components

Route components receive a `RouteParams` argument:

```saffron
fun UserPage(route: RouteParams): Node {
    var user_id = route.get("id")
    var user = resource(fun () => fetch_user(user_id))

    return div {
        match (user.get()) {
            Loading => p { text("Loading...") }
            is Ready(u) => div {
                h1 { text(u.name) }
                p { text(u.email) }
            }
            is Error(msg) => p { text("Error: ${msg}") }
        }
    }
}
```

## Navigation

### Links

```saffron
fun NavBar(): Node {
    return nav {
        Router.link("/", [class_("nav-link")]) { text("Home") }
        Router.link("/about", [class_("nav-link")]) { text("About") }
        Router.link("/users/42", []) { text("User 42") }
    }
}
```

`Router.link` renders an `<a>` tag that navigates without a full page reload.

### Programmatic Navigation

```saffron
Router.navigate("/users/42")
Router.navigate("/search?q=hello")
Router.back()
Router.forward()
```

## Active Route Signal

The current route is a signal — components can react to route changes:

```saffron
var current = Router.current_route()

effect(fun () => {
    IO.println("Navigated to: ${current.get().path}")
})
```

## Nested Routes

```saffron
fun DashboardLayout(): Node {
    return div([class_("dashboard")]) {
        Sidebar()
        div([class_("content")]) {
            Router.outlet()  // renders the matched child route
        }
    }
}

var routes = [
    Route("/dashboard", DashboardLayout, [
        Route("/dashboard/overview", OverviewPage),
        Route("/dashboard/settings", SettingsPage),
        Route("/dashboard/users/:id", UserDetailPage),
    ]),
]
```

## Guards

Protect routes with guard functions that run before navigation:

```saffron
fun require_auth(route: RouteParams): Bool {
    return auth_state.get().is_logged_in
}

var routes = [
    Route("/public", PublicPage),
    Route("/admin", AdminPage).guard(require_auth),
]
```

If a guard returns `false`, navigation is cancelled and the current route stays.

## Query Parameters

Access query parameters from the route:

```saffron
// URL: /search?q=hello&page=2
fun SearchPage(route: RouteParams): Node {
    var query = route.query.get("q")       // "hello"
    var page = route.query.get("page")     // "2"

    return div {
        h1 { text("Results for: ${query}") }
    }
}
```

## Implementation Notes

The router uses the browser's History API (`pushState`/`popstate`) under the hood. In WASM, this means:

1. `Router.navigate()` calls a JS-imported function to push state
2. `popstate` events from the browser call back into WASM to update the route signal
3. Components that read `current_route()` automatically re-render via the signal system

No framework magic — just signals and JS interop.
