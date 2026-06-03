# Set

```saffron
import "@set" as Sets
```

A set data structure backed by a Map, with standard set operations.

## Creating a Set

```saffron
import "@set" as Sets

var s = Sets.Set()
s.add(1).add(2).add(3)
```

## Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `s.add(value)` | `Set` | Add an element (chainable) |
| `s.remove(value)` | `Set` | Remove an element (chainable) |
| `s.has(value)` | `Bool` | Check membership |
| `s.size()` | `Number` | Number of elements |
| `s.values()` | `List` | All values as a list |

## Creating from a list

```saffron
var s = Sets.from([1, 2, 3, 2, 1])
s.size()  // 3
```

## Set Operations

| Function | Returns | Description |
|----------|---------|-------------|
| `Sets.from(items)` | `Set` | Create a Set from a list |
| `Sets.union(a, b)` | `Set` | Elements in either set |
| `Sets.intersect(a, b)` | `Set` | Elements in both sets |
| `Sets.diff(a, b)` | `Set` | Elements in a but not b |

## Example

```saffron
import "@set" as Sets

var a = Sets.from([1, 2, 3, 4])
var b = Sets.from([3, 4, 5, 6])

var both = Sets.intersect(a, b)
IO.println(both.values())  // [3, 4]

var either = Sets.union(a, b)
IO.println(either.size())  // 6

var only_a = Sets.diff(a, b)
IO.println(only_a.values())  // [1, 2]

// Membership
IO.println(a.has(2))   // true
IO.println(a.has(99))  // false
```
