# Iterators

## For-in loops

`for-in` walks a collection by index. It is not built on a protocol — the parser
rewrites it into a `while` loop over `length()` and `[i]`:

```saffron
for (item in [10, 20, 30]) {
    IO.println(item)
}
```

That means it works on exactly the receivers that support `length()` and integer
indexing:

```saffron
// Lists — element by element
for (n in [1, 2, 3]) {
    IO.println(n)
}

// Strings — character by character
for (ch in "abc") {
    IO.println(ch)  // a, b, c
}
```

### Maps do not work yet

`for-in` over a Map **segfaults** (BUGS #62), with or without a type annotation,
because a Map has no integer indexing for the desugaring to use. Go through
`keys()` instead:

```saffron
var ages: Map<String, Int> = {"ada": 36, "alan": 41}
var names: List<String> = ages.keys()
for (name in names) {
    IO.println(name)
}
```

`ages.get(name)` returns `Int|Nil`, so nil-check it before using it as an `Int`.

## The iterator protocol is not implemented

The prelude declares two interfaces:

```saffron
interface Iterator<T> {
    fun next(): T
    fun has_next(): Bool
}

interface Iterable<T> {
    fun iter(): Iterator<T>
}
```

Nothing implements them. `iter()` does not exist on any builtin collection, and
`for-in` does not call it. Both ways of reaching the protocol fail, and they fail
differently (BUGS #62):

```saffron
// A manual cursor loop compiles, exits 0, and prints NOTHING.
var list = [10, 20, 30]
var iter = list.iter()
while (iter.has_next()) {
    IO.println(iter.next())   // never runs
}
```

```saffron
// for-in over a class that implements the protocol is a compile error:
// [codegen] Error: type 'IntRange' has no method 'length'
for (i in IntRange(0, 5)) { ... }
```

Use `for-in` over a List or String, or a plain `while` loop with your own index,
until the protocol is implemented.

## The `@iter` module

The standard library provides functional iteration utilities that accept any `Iterable<T>`:

```saffron
import "@iter" as Iter

var numbers = [1, 2, 3, 4, 5]

var doubled = Iter.map(numbers, fun (x: Int): Int => x * 2)
// [2, 4, 6, 8, 10]

var evens = Iter.filter(numbers, fun (x: Int): Bool => x % 2 == 0)
// [2, 4]

var total = Iter.reduce(numbers, fun (acc: Int, x: Int): Int => acc + x, 0)
// 15
```

Other functions: `any`, `all`, `each`, `count`, `find`, `flat_map`, `zip`, `enumerate`, `take`, `skip`, `sum`.

## Named imports

Import specific functions directly:

```saffron
import { map, filter, reduce } from "@iter"

var result = filter([1, 2, 3, 4], fun (x: Int): Bool => x > 2)
// [3, 4]
```

## Composing transformations

Compose by nesting calls, or — usually clearer — by naming each step:

```saffron
import { map, filter } from "@iter"

// Nested
var result = map(
    filter([1, 2, 3, 4, 5], fun (x: Int): Bool => x % 2 == 0),
    fun (x: Int): Int => x * 10
)
IO.println(result)  // [20, 40]

// Named steps read top-to-bottom
var evens = filter([1, 2, 3, 4, 5], fun (x: Int): Bool => x % 2 == 0)
var scaled = map(evens, fun (x: Int): Int => x * 10)
IO.println(scaled)  // [20, 40]
```

## Making your own type iterable

You cannot, yet. Implementing `iter()` does not help, because `for-in` never
calls it. Implementing `length()` and `getItem()` does not help either: indexing
has no dispatch path for user classes, so `obj[i]` runs the list accessor against
your object and segfaults (BUGS #62).

Expose a `List` and iterate that:

```saffron
class IntRange {
    var start: Int
    var end: Int

    fun init(start: Int, end: Int) {
        this.start = start
        this.end = end
    }

    fun to_list(): List<Int> {
        var out: List<Int> = []
        var i: Int = this.start
        while (i < this.end) {
            out.push(i)
            i = i + 1
        }
        return out
    }
}

for (i in IntRange(0, 5).to_list()) {
    IO.println(i)  // 0, 1, 2, 3, 4
}
```

This materializes eagerly, so it is unsuitable for a large or infinite sequence —
write those as an explicit `while` loop over your own cursor.

`@iter`'s functions take `List<T>` rather than `Iterable<T>` for the same reason.
`src/lib/iter.sf` explains the constraint in its module doc: widening those
signatures needs an `Iterable` interface the checker understands, plus a `for-in`
that can reach non-list receivers.
