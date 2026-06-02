# Base64

```saffron
import "@base64" as Base64
```

Base64 encoding and decoding (RFC 4648).

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Base64.encode(input)` | `String` | Standard Base64 encoding (with `=` padding) |
| `Base64.decode(input)` | `String` | Decode standard Base64 |
| `Base64.encode_url_safe(input)` | `String` | URL-safe encoding (`-_` instead of `+/`, no padding) |
| `Base64.decode_url_safe(input)` | `String` | Decode URL-safe Base64 |
| `Base64.is_valid(input)` | `Bool` | Check if a string is valid Base64 |

## Example

```saffron
import "@base64" as Base64

var encoded = Base64.encode("Hello, Saffron!")
IO.println(encoded)  // "SGVsbG8sIFNhZmZyb24h"

var decoded = Base64.decode(encoded)
IO.println(decoded)  // "Hello, Saffron!"

// URL-safe variant (for use in URLs and filenames)
var url_safe = Base64.encode_url_safe("data with +/= characters")
IO.println(url_safe)

// Validation
IO.println(Base64.is_valid("SGVsbG8="))   // true
IO.println(Base64.is_valid("not valid!")) // false
```
