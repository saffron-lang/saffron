# Style Guide

Conventions for writing clear, consistent Saffron code.

## Naming

| Kind | Convention | Example |
|------|-----------|---------|
| Variables | `snake_case` | `user_count`, `max_retries` |
| Functions | `snake_case` | `parse_int`, `read_file` |
| Classes | `PascalCase` | `HttpClient`, `FileReader` |
| Enums | `PascalCase` | `Option`, `Result` |
| Enum variants | `PascalCase` | `Some`, `None`, `Ok`, `Err` |
| Constants | `snake_case` | `pi`, `max_buffer_size` |
| Modules (import alias) | `PascalCase` | `Math`, `Collections` |
| Type parameters | Single uppercase letter | `T`, `K`, `V` |

## Import aliases

Always use descriptive names — avoid single-character aliases:

```saffron
// Good
import "@collections" as Collections
import "@test" as Test
import "@math" as Math

// Bad
import "@collections" as C
import "@test" as T
import "@math" as M
```

Prefer named imports when importing utility functions you'll call directly:

```saffron
// Good — named imports for utilities
import { json, not_found, bad_request } from "parsley/response"
import { param, query_params } from "parsley/request"

// Good — module alias for namespaces with many symbols
import "@http/server" as Http
import "turmeric/router" as Router

// Bad — single-letter alias
import "parsley/router" as P
import "basil/query" as Q
```

## Indentation and braces

Use 4 spaces. Opening brace on the same line:

```saffron
fun fibonacci(n: Number): Number {
    if (n <= 1) {
        return n
    }
    return fibonacci(n - 1) + fibonacci(n - 2)
}
```

## Line length

Prefer lines under 100 characters. Break long function calls or chains:

```saffron
// Name intermediate steps instead of deeply nesting calls
var big = Iter.filter(items, fun (x: Number): Bool => x > threshold)
var scaled = Iter.map(big, fun (x: Number): Number => x * 2)
var result = Iter.sum(scaled)

// Break long parameter lists
fun create_user(
    name: String,
    email: String,
    age: Number,
    role: Role
): User {
    // ...
}
```

## Type annotations

Annotate function parameters and return types. Omit on local variables when the type is obvious:

```saffron
// Parameters and returns: always annotate
fun distance(a: Point, b: Point): Number {
    var dx = a.x - b.x   // type obvious from context
    var dy = a.y - b.y
    return Math.sqrt(dx * dx + dy * dy)
}

// Annotate locals when the type isn't obvious
var config: Map<String, String> = parse_config(text)
```

## Enum and match style

Align match arms. Keep short arms on one line:

```saffron
var message = match (result) {
    Ok(value) => "success: ${value}",
    Err(e) => "error: ${e}"
}
```

For multi-line arms, use a block:

```saffron
match (command) {
    Quit => {
        save_state()
        OS.exit(0)
    },
    Run(script) => {
        execute(script)
    }
}
```

## Imports

Group imports by category — builtins first, then relative files. One blank line between groups:

```saffron
import "@math" as Math
import "@collections" as Collections
import "@iter" as Iter

import "./models/user.sf" as User
import "./utils/validate.sf" as Validate
```

## Comments and documentation

Use `///` doc-comments on public functions and classes. Skip comments for obvious code:

```saffron
/// Parse a duration string like "5s", "100ms", or "2m" into seconds.
fun parse_duration(input: String): Number {
    // ...
}

/// HTTP client with connection pooling.
class HttpClient {
    // ...
}
```

Use `//!` at the top of a file for module-level documentation:

```saffron
//! Utilities for working with file paths.
//! Handles both Unix and Windows separators.

fun join(parts: List<String>): String {
    // ...
}
```

## Error handling

Prefer `Result` for expected failures. Reserve `throw` for unexpected/unrecoverable errors:

```saffron
// Expected failure — use Result
fun parse_config(text: String): Result<Config, String> {
    if (text.length() == 0) {
        return Result.Err("empty config")
    }
    // ...
}

// Unexpected failure — throw
fun get_required_env(name: String): String {
    var value = OS.env(name)
    if (value is Nil) {
        throw "required environment variable '${name}' not set"
    }
    return value
}
```

## Class design

Put fields first, then `init`, then methods:

```saffron
class Connection {
    var host: String
    var port: Number
    var timeout: Number

    fun init(host: String, port: Number) {
        this.host = host
        this.port = port
        this.timeout = 30.0
    }

    fun connect(): Result<Socket, String> {
        // ...
    }

    fun close() {
        // ...
    }
}
```
