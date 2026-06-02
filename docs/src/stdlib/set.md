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

## Set Operations

| Function | Returns | Description |
|----------|---------|-------------|
| `Sets.union(a, b)` | `Set` | Elements in either set |
| `Sets.intersection(a, b)` | `Set` | Elements in both sets |
| `Sets.difference(a, b)` | `Set` | Elements in a but not b |

## Example

```saffron
import "@set" as Sets

var a = Sets.Set()
a.add(1).add(2).add(3).add(4)

var b = Sets.Set()
b.add(3).add(4).add(5).add(6)

var both = Sets.intersection(a, b)
IO.println(both.values())  // [3, 4]

var either = Sets.union(a, b)
IO.println(either.size())  // 6

var only_a = Sets.difference(a, b)
IO.println(only_a.values())  // [1, 2]

// Membership
IO.println(a.has(2))   // true
IO.println(a.has(99))  // false
```
