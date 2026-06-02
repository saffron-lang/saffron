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

## Example

```saffron
import "@toml" as TOML

var source = "[server]\nhost = \"localhost\"\nport = 8080\n\n[database]\nurl = \"postgres://localhost/app\""
var config = TOML.parse(source)

var host = config.get("server").get("host")
var port = config.get("server").get("port")
IO.println("Server: ${host}:${port}")
```
