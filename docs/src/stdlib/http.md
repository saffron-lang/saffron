# HTTP

```saffron
import "@http" as Http
```

An HTTP/1.1 client with support for GET, POST, PUT, DELETE, PATCH, headers, redirects, and JSON convenience methods.

## Response class

All request functions return a `Response`:

```saffron
class Response {
    var status: Number
    var status_text: String
    var headers: Map<String, String>
    var body: String
    var url: String
}
```

## Functions

### `Http.get(url: String): Response`

Make a GET request:

```saffron
var resp = Http.get("https://api.example.com/users")
IO.println(resp.status)  // 200
IO.println(resp.body)
```

### `Http.get_json(url: String)`

GET and parse the response as JSON:

```saffron
var data = Http.get_json("https://api.example.com/users/1")
IO.println(data.get("name"))
```

### `Http.post(url: String, body: String): Response`

Make a POST request with a string body:

```saffron
var resp = Http.post("https://api.example.com/data", "raw body content")
```

### `Http.post_json(url: String, data): Response`

POST JSON data (automatically serializes and sets Content-Type):

```saffron
var resp = Http.post_json("https://api.example.com/users", {
    "name": "alice",
    "email": "alice@example.com"
})
```

### `Http.put(url: String, body: String): Response`

### `Http.put_json(url: String, data): Response`

### `Http.patch_json(url: String, data): Response`

### `Http.delete(url: String): Response`

### `Http.download(url: String, dest_path: String): Bool`

Download a file to a local path. Returns `true` on success.

### `Http.request(opts: RequestOptions): Response`

Low-level request with full control:

```saffron
var resp = Http.request(RequestOptions(
    "POST",
    "https://api.example.com/upload",
    {"Authorization": "Bearer token123", "Content-Type": "text/plain"},
    "file contents here"
))
```

## Example

```saffron
import "@http" as Http
import "@json" as JSON

var resp = Http.get("https://httpbin.org/get")
if (resp.status == 200) {
    var data = JSON.parse(resp.body)
    IO.println(data.get("origin"))
}
```
