# Lists and Maps

## Lists

Lists are ordered, growable collections:

```saffron
var numbers = [1, 2, 3]
var names: List<String> = ["alice", "bob"]
```

### Indexing

Zero-based, with negative indexing from the end:

```saffron
var list = [10, 20, 30]
IO.println(list[0])   // 10
IO.println(list[-1])  // 30
```

### Methods

```saffron
var list = [1, 2, 3]

list.push(4)        // [1, 2, 3, 4]
list.pop()          // returns 4, list is [1, 2, 3]
list.length()       // 3
list.reverse()      // [3, 2, 1]
list.sort()         // [1, 2, 3]
list.copy()         // shallow copy
```

### Iteration

```saffron
for (item in [10, 20, 30]) {
    IO.println(item)
}
```

## Maps

Maps are key-value collections:

```saffron
var scores: Map<String, Int> = {"alice": 95, "bob": 87}
```

### Operations

```saffron
var m: Map<String, Int> = {}

m.set("x", 10)
m.get("x")       // 10
m.has("x")       // true
m.keys()         // list of keys
m.values()       // list of values
m.length()       // number of entries
```

### Iteration

Maps are iterable and yield `[key, value]` pairs. The idiomatic form is a for-in loop:

```saffron
var m = {"a": 1, "b": 2}

for (entry in m) {
    IO.println("${entry[0]} = ${entry[1]}")
}
```

Or drive the iterator by hand with `.iter()`, `has_next()`, and `next()`:

```saffron
var iter = m.iter()
while (iter.has_next()) {
    var entry = iter.next()
    IO.println("${entry[0]} = ${entry[1]}")
}
```
