# TOML

```saffron
import "@toml" as TOML
```

Parse and serialize TOML configuration files.

## Functions

### `TOML.parse(source: String): Map<String, Any>`

Parse a TOML string into a nested Map:

```saffron
var config = TOML.parse("[package]\nname = \"saffron\"\nversion = \"0.1.0\"")
IO.println(config.get("package").get("name"))     // "saffron"
IO.println(config.get("package").get("version"))  // "0.1.0"
```

### `TOML.parse_file(path: String): Map<String, Any>`

Read and parse a TOML file:

```saffron
var config = TOML.parse_file("config.toml")
```

### `TOML.stringify(data): String`

Serialize a Map to TOML format:

```saffron
var config = {"name": "my-app", "version": "1.0.0"}
IO.println(TOML.stringify(config))
```

## Supported TOML features

- Key-value pairs
- Tables (`[section]`)
- Nested tables (`[section.subsection]`)
- Dotted keys (`a.b.c = value`)
- Strings (basic and literal)
- Integers and floats
- Booleans
- Arrays
- Inline tables
- Array of tables (`[[items]]`)
- Comments (`#`)

### `TOML.load(path: String): TomlTable`

Parse a TOML file and return a typed `TomlTable` wrapper with typed accessors:

```saffron
var config = TOML.load("pantry.toml")
var pkg = config.table("package")
var name: String = pkg.string("name")
var version: String = pkg.string_or("version", "0.1.0")
```

### `TOML.load_string(source: String): TomlTable`

Same as `load` but takes a string instead of a file path.

### TomlTable methods

| Method | Returns | Description |
|--------|---------|-------------|
| `t.string(key)` | `String` | Get string value (throws if missing) |
| `t.string_or(key, default)` | `String` | Get string or default |
| `t.number(key)` | `Any` | Get number value (`Int` or `Float`, per the spelling in the file) |
| `t.number_or(key, default)` | `Any` | Get number or default |
| `t.bool(key)` | `Bool` | Get boolean value |
| `t.bool_or(key, default)` | `Bool` | Get boolean or default |
| `t.table(key)` | `TomlTable` | Get sub-table |
| `t.table_or(key)` | `TomlTable` | Get sub-table or empty |
| `t.list(key)` | `List<Any>` | Get array value |
| `t.list_or(key)` | `List<Any>` | Get array or empty list |
| `t.has(key)` | `Bool` | Check if key exists |
| `t.keys()` | `List<String>` | All keys |
| `t.get(key)` | `Any` | Raw value access |
| `t.to_map()` | `Map<String, Any>` | Get underlying map |

## Example

```saffron
import "@toml" as TOML

var source = "[server]\nhost = \"localhost\"\nport = 8080\n\n[database]\nurl = \"postgres://localhost/app\""
var config = TOML.parse(source)

var host = config.get("server").get("host")
var port = config.get("server").get("port")
IO.println("Server: ${host}:${port}")
```
