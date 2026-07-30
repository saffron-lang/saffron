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
- Object -> `Map<String, Any>`
- Array -> `List`
- String -> `String`
- Number -> `Int` or `Float` (per the spelling of the literal)
- Boolean -> `Bool`
- null -> `Nil`

### `JSON.to_string(value): String`

Serialize a Saffron value to a compact JSON string:

```saffron
var data = {"name": "saffron", "tags": ["lang", "typed"]}
var text = JSON.to_string(data)
IO.println(text)  // {"name":"saffron","tags":["lang","typed"]}
```

### `JSON.pretty(value): String`

Pretty-print with indentation:

```saffron
IO.println(JSON.pretty(data))
// {
//   "name": "saffron",
//   "tags": [
//     "lang",
//     "typed"
//   ]
// }
```

### `JSON.parse_into(klass, source): Any`

Parse JSON and construct an instance of the given class:

```saffron
var instance = JSON.parse_into(MyClass, json_string)
```

## Example: reading a config file

```saffron
import "@json" as JSON

var config = JSON.parse(IO.read_file("config.json"))
var port = config.get("port")
IO.println("Starting on port ${port}")
```
