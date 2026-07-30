# URL

```saffron
import "@url" as URL
```

Parse, construct, and manipulate URLs per RFC 3986.

## URL class

```saffron
class URL {
    var scheme: String
    var username: String
    var password: String
    var host: String
    var port: Int
    var path: String
    var query: String
    var fragment: String
}
```

## Functions

### Parsing and formatting

| Function | Returns | Description |
|----------|---------|-------------|
| `URL.parse(url_str)` | `URL` | Parse a URL string into components |
| `URL.to_string(url)` | `String` | Format a URL back to string |

### Query string

| Function | Returns | Description |
|----------|---------|-------------|
| `URL.parse_query(query_str)` | `Map<String, String>` | Parse query string into key-value pairs |
| `URL.encode_query(params)` | `String` | Encode a map as a query string |

### Encoding

| Function | Returns | Description |
|----------|---------|-------------|
| `URL.encode(s)` | `String` | Percent-encode a string |
| `URL.decode(s)` | `String` | Decode a percent-encoded string |

### Resolution

| Function | Returns | Description |
|----------|---------|-------------|
| `URL.resolve(base, relative)` | `URL` | Resolve a relative URL against a base |
| `URL.join(base_str, path)` | `String` | Join a base URL with a relative path |
| `URL.effective_port(url)` | `Int` | Get port (uses default if not specified) |

## Example

```saffron
import "@url" as URL

var u = URL.parse("https://user:pass@example.com:8080/path?q=hello&lang=en#section")
IO.println(u.scheme)    // "https"
IO.println(u.host)      // "example.com"
IO.println(u.port)      // 8080
IO.println(u.path)      // "/path"
IO.println(u.query)     // "q=hello&lang=en"
IO.println(u.fragment)  // "section"
IO.println(u.username)  // "user"

// Query parsing
var params = URL.parse_query(u.query)
IO.println(params.get("q"))     // "hello"
IO.println(params.get("lang"))  // "en"

// URL joining
var full = URL.join("https://example.com/api/", "../v2/users")
IO.println(full)  // "https://example.com/v2/users"

// Percent encoding
IO.println(URL.encode("hello world"))  // "hello%20world"
IO.println(URL.decode("hello%20world"))  // "hello world"
```
