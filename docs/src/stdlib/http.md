# HTTP

Saffron provides separate modules for the HTTP client and server.

## HTTP Client

```saffron
import "@http/client" as Http
```

An async HTTP/1.1 client with support for GET, POST, PUT, DELETE, PATCH, HEAD, headers, redirects, TLS, and JSON convenience methods.

### Response class

All request functions return a `Response`:

```saffron
class Response {
    var status: Int
    var status_text: String
    var headers: Map<String, String>
    var body: String
    var url: String
}
```

Response methods:

| Method | Returns | Description |
|--------|---------|-------------|
| `resp.ok()` | `Bool` | True if status is 2xx |
| `resp.json()` | value | Parse body as JSON |
| `resp.header(name)` | `String` | Get header value (case-insensitive) |
| `resp.raise_for_status()` | -- | Throw if status >= 400 |
| `resp.is_client_error()` | `Bool` | True if 4xx |
| `resp.is_server_error()` | `Bool` | True if 5xx |

### Functions

#### `Http.get(url: String): Response`

```saffron
var resp = Http.get("https://api.example.com/users")
IO.println(resp.status)  // 200
IO.println(resp.body)
```

#### `Http.get_with_headers(url, headers): Response`

```saffron
var resp = Http.get_with_headers("https://api.example.com", {"Accept": "text/html"})
```

#### `Http.get_json(url: String): Any`

GET and parse the response as JSON (raises on non-2xx):

```saffron
var data = Http.get_json("https://api.example.com/users/1")
IO.println(data.get("name"))
```

#### `Http.post(url, body, headers): Response`

```saffron
var resp = Http.post("https://api.example.com/data", "body content",
    {"Content-Type": "text/plain"})
```

#### `Http.post_body(url, body): Response`

POST with no custom headers:

```saffron
var resp = Http.post_body("https://api.example.com/data", "body content")
```

#### `Http.post_json(url, data): Response`

POST JSON data (automatically serializes and sets Content-Type):

```saffron
var resp = Http.post_json("https://api.example.com/users", {
    "name": "alice",
    "email": "alice@example.com"
})
```

#### `Http.put(url, body, headers): Response`

#### `Http.put_body(url, body): Response`

#### `Http.put_json(url, data): Response`

#### `Http.patch(url, body, headers): Response`

#### `Http.patch_json(url, data): Response`

#### `Http.delete(url): Response`

#### `Http.delete_with_headers(url, headers): Response`

#### `Http.head(url): Response`

#### `Http.request(method, url, body, headers): Response`

Full-control request (follows up to 5 redirects):

```saffron
var resp = Http.request("PUT", "https://api.example.com/items/1",
    "{\"price\": 42}",
    {"Content-Type": "application/json", "Authorization": "Bearer tok"})
```

#### `Http.request_no_redirect(method, url, body, headers): Response`

Same as `request` but does not follow redirects.

### Example

```saffron
import "@http/client" as Http
import "@json" as JSON

var resp = Http.get("https://httpbin.org/get")
if (resp.ok()) {
    var data = resp.json()
    IO.println(data.get("origin"))
}
```

## HTTP Server

```saffron
import "@http/server" as Http
```

An async HTTP/1.1 server with routing, middleware, streaming, and TLS support.

### Creating a server

```saffron
var app = Http.server(8080)

app.get("/", fun (req) => Http.html("<h1>Hello!</h1>"))

app.get("/api/users", fun (req) =>
    Http.json("{\"users\": []}")
        .header("Cache-Control", "max-age=60"))

app.serve()
```

### Route methods

| Method | Description |
|--------|-------------|
| `app.get(path, handler)` | Register GET handler |
| `app.post(path, handler)` | Register POST handler |
| `app.put(path, handler)` | Register PUT handler |
| `app.delete(path, handler)` | Register DELETE handler |
| `app.all(path, handler)` | Register handler for all methods |

### Route parameters

```saffron
app.get("/users/:id", fun (req) =>
    Http.text("User: ${req.param("id")}"))
```

### Response builders

| Function | Description |
|----------|-------------|
| `Http.text(body)` | 200 with text/plain |
| `Http.html(body)` | 200 with text/html |
| `Http.json(body)` | 200 with application/json |
| `Http.file(content, mime)` | 200 with custom MIME type |
| `Http.redirect(url)` | 302 redirect |
| `Http.not_found()` | 404 |
| `Http.error(message)` | 500 |
| `Http.event_stream(handler)` | SSE streaming response |

### Middleware

```saffron
// Before-middleware (runs before route handler)
app.use(Http.logger())
app.use(Http.cors())
app.use(Http.static_files("/static", "./public"))

// After-middleware (can modify the response)
app.after(Http.cors_headers())
```

### Streaming (Server-Sent Events)

```saffron
app.get("/events") { req =>
    req.stream { stream =>
        stream.send("hello")
        stream.send_event("update", "data here")
        stream.close()
    }
}
```

### TLS

```saffron
import "@http/server" as Http
import "@ssl" as SSL

var tls_ctx = SSL.server_context({"cert": "cert.pem", "key": "key.pem"})
var app = Http.server_tls(443, tls_ctx)
app.get("/") { req => Http.text("Hello over HTTPS!") }
app.serve()
```
