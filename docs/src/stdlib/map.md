# Map

Maps are built-in — no import needed.

## Creating maps

```saffron
var empty: Map<String, Int> = {}
var scores = {"alice": 95, "bob": 87}
```

## Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `m.get(key)` | value or nil | Look up by key |
| `m.set(key, value)` | -- | Insert or update |
| `m.has(key)` | `Bool` | Check if key exists |
| `m.keys()` | `List` | All keys |
| `m.values()` | `List` | All values |
| `m.length()` | `Int` | Number of entries |

## Iteration

Maps implement the iterator protocol. Each iteration yields a `[key, value]` pair:

```saffron
var m = {"x": 1, "y": 2, "z": 3}

for (entry in m) {
    IO.println("${entry[0]} = ${entry[1]}")
}
```

## Example

```saffron
var counts: Map<String, Int> = {}

var words = ["hello", "world", "hello", "saffron", "world", "hello"]
for (word in words) {
    if (counts.has(word)) {
        counts.set(word, counts.get(word) + 1)
    } else {
        counts.set(word, 1)
    }
}

IO.println(counts)  // {"hello": 3, "world": 2, "saffron": 1}
```
