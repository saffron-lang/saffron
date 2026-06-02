# List

Lists are built-in — no import needed.

## Creating lists

```saffron
var empty: List<Number> = []
var nums = [1, 2, 3]
var mixed: List<String> = ["a", "b", "c"]
```

## Indexing

```saffron
var list = [10, 20, 30]
list[0]    // 10
list[-1]   // 30 (last element)
list[1]    // 20
```

## Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `list.push(item)` | — | Append to end |
| `list.pop()` | element | Remove and return last |
| `list.length()` | `Number` | Number of elements |
| `list.reverse()` | `List` | Reversed copy |
| `list.sort()` | `List` | Sorted copy |
| `list.copy()` | `List` | Shallow copy |
| `list.contains(item)` | `Bool` | Check membership |
| `list.index_of(item)` | `Number` | Index of first occurrence (-1 if absent) |
| `list.slice(start, end)` | `List` | Sub-list by index range |
| `list.join(sep)` | `String` | Join elements with separator |

## Iteration

```saffron
for (item in [1, 2, 3]) {
    IO.println(item)
}
```

## With `@iter`

```saffron
import { map, filter } from "@iter"

var squares = map([1, 2, 3, 4], fun (x: Number): Number => x * x)
// [1, 4, 9, 16]
```
