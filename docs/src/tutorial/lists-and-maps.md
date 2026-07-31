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

`for-in` over a Map **segfaults** (BUGS #62) — with or without a type
annotation. `keys()` and `values()` are ordered consistently, so walk them
together by index:

```saffron
var m: Map<String, Int> = {"a": 1, "b": 2}
var ks: List<String> = m.keys()
var vs: List<Int> = m.values()

for (i = 0; i < ks.length(); i = i + 1) {
    IO.println("${ks[i]} = ${vs[i]}")   // a = 1, b = 2
}
```

Going through `m.get(k)` instead is more awkward than it looks, because it
returns `Int|Nil` and nil-narrowing does not work on it: both `if (v is Nil) {
continue }` and `if (!(v is Nil)) { ... }` still fail to compile with "cannot
call .to_string() on nullable 'v'". Prefer the `values()` form above when you
need the values.

For keys alone, `for-in` over `keys()` is fine:

```saffron
for (k in ks) {
    IO.println(k)
}
```

The `.iter()` / `has_next()` / `next()` form some documentation showed does not
work — nothing implements it, and the loop silently does nothing (BUGS #62).
