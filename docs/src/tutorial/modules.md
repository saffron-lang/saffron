# Modules and Imports

## Import syntax

Saffron has two kinds of imports — built-in modules and relative file imports:

```saffron
import "@math" as Math            // built-in module
import "../utils/helpers.sf" as Helpers // relative file path
```

## Built-in modules (`@` prefix)

All built-in modules use the `@` prefix. These are bundled with the interpreter and always available:

| Module | Description |
|--------|-------------|
| `@math` | Math functions and constants |
| `@iter` | Functional iteration utilities |
| `@time` | Time and date utilities |
| `@os` | Operating system interaction |
| `@json` | JSON parsing and serialization |
| `@toml` | TOML parsing and serialization |
| `@csv` | CSV parsing |
| `@reflect` | Runtime type introspection |
| `@random` | Random number generation |
| `@test` | Test framework |
| `@fmt` | String formatting |
| `@set` | Set data structure |
| `@path` | File path manipulation |
| `@glob` | Glob pattern matching and file discovery |
| `@url` | URL parsing and manipulation |
| `@http` | HTTP client |
| `@base64` | Base64 encoding/decoding |
| `@uuid` | UUID generation and parsing |
| `@semver` | Semantic version parsing and comparison |
| `@log` | Structured logging |
| `@async` | Async task utilities |
| `@lexer` | Tokenize Saffron source |
| `@parser` | Parse source into AST |
| `@ast` | AST types and traversal |

```saffron
import "@iter" as Iter
import "@math" as Math
import "@json" as JSON
import "@test" as Test
```

## Named imports

Import specific names directly from a module:

```saffron
import { map, filter, reduce } from "@iter"

var doubled = map([1, 2, 3], fun (x: Number): Number => x * 2)
```

## Using imported modules

Imported names act as namespaces:

```saffron
import "@math" as Math
import "@json" as JSON

IO.println(Math.pi)           // 3.14159...
IO.println(Math.sqrt(16.0))   // 4.0

var data = JSON.parse("{\"x\": 1}")
IO.println(data.get("x"))    // 1
```

## Writing your own modules

Any `.sf` file is a module. Top-level `fun`, `class`, `enum`, and `var` declarations are exported:

```saffron
// geometry.sf
fun circle_area(radius: Number): Number {
    return 3.14159 * radius * radius
}

class Rectangle {
    var width: Number
    var height: Number
    fun init(w: Number, h: Number) {
        this.width = w
        this.height = h
    }
    fun area(): Number {
        return this.width * this.height
    }
}
```

```saffron
// main.sf
import "./geometry.sf" as Geo

IO.println(Geo.circle_area(5.0))

var rect = Geo.Rectangle(3.0, 4.0)
IO.println(rect.area())  // 12.0
```
