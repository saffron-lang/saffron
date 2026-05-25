# Set

```saffron
import "@set" as Set
```

A set is an unordered collection of unique values.

## Creating sets

```saffron
var s = Set.new()
var from_list = Set.from([1, 2, 3, 2, 1])  // {1, 2, 3}
```

## Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `s.add(item)` | — | Add an element |
| `s.remove(item)` | — | Remove an element |
| `s.has(item)` | `Bool` | Check membership |
| `s.length()` | `Int` | Number of elements |
| `s.to_list()` | `List` | Convert to list |

## Set operations

| Function | Description |
|----------|-------------|
| `Set.union(a, b)` | Elements in either set |
| `Set.intersection(a, b)` | Elements in both sets |
| `Set.difference(a, b)` | Elements in a but not b |

## Example

```saffron
import "@set" as Set

var a = Set.from([1, 2, 3, 4])
var b = Set.from([3, 4, 5, 6])

var both = Set.intersection(a, b)
IO.println(both.to_list())  // [3, 4]
```
