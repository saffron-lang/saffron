# JSON

```saffron
import "@json" as JSON
```

Parse and serialize JSON data.

## Functions

### `JSON.parse(text: String)`

Parse a JSON string into Saffron values:

```saffron
var data = JSON.parse("{\"name\": \"saffron\", \"version\": 1}")
IO.println(data.get("name"))     // saffron
IO.println(data.get("version"))  // 1
```

JSON types map to Saffron types:
- Object → `Map<String, Any>`
- Array → `List`
- String → `String`
- Number → `Int` or `Float`
- Boolean → `Bool`
- null → `Nil`

### `JSON.stringify(value): String`

Serialize a Saffron value to a JSON string:

```saffron
var data = {"name": "saffron", "tags": ["lang", "typed"]}
var text = JSON.stringify(data)
IO.println(text)  // {"name":"saffron","tags":["lang","typed"]}
```

### `JSON.stringify(value, indent: Int): String`

Pretty-print with indentation:

```saffron
IO.println(JSON.stringify(data, 2))
```

## Example: reading a config file

```saffron
import "@json" as JSON

var config = JSON.parse(IO.read_file("config.json"))
var port = config.get("port")
IO.println("Starting on port ${port}")
```
