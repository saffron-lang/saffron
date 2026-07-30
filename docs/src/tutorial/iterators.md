# Iterators

## The iterator protocol

Iteration is built on two interfaces declared in the prelude:

```saffron
interface Iterator<T> {
    fun next(): T
    fun has_next(): Bool
}

interface Iterable<T> {
    fun iter(): Iterator<T>
}
```

An **iterable** exposes `.iter()`, which returns an **iterator**. An iterator answers
`has_next()` to say whether another element is available, and `next()` to produce it.

Lists, strings, and maps are all iterable out of the box.

## Manual iteration

```saffron
var list = [10, 20, 30]
var iter = list.iter()

while (iter.has_next()) {
    IO.println(iter.next())
}
```

## For-in loops

`for-in` is syntactic sugar for the protocol above — it calls `.iter()` once, then loops
while `has_next()` is true, binding each `next()` to the loop variable:

```saffron
for (item in [10, 20, 30]) {
    IO.println(item)
}
```

It works over anything iterable:

```saffron
// Lists — element by element
for (n in [1, 2, 3]) {
    IO.println(n)
}

// Strings — character by character
for (ch in "abc") {
    IO.println(ch)  // a, b, c
}

// Maps — [key, value] pairs
for (entry in {"a": 1, "b": 2}) {
    IO.println("${entry[0]} = ${entry[1]}")
}
```

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

## Writing iterable classes

Make your own type work with `for-in` by implementing the protocol — an `iter()` method
returning an object with `has_next()` and `next()`:

```saffron
class IntRange {
    var start: Int
    var end: Int

    fun init(start: Int, end: Int) {
        this.start = start
        this.end = end
    }

    fun iter(): RangeIterator {
        return RangeIterator(this.start, this.end)
    }
}

class RangeIterator {
    var current: Int
    var end: Int

    fun init(start: Int, end: Int) {
        this.current = start
        this.end = end
    }

    fun has_next(): Bool {
        return this.current < this.end
    }

    fun next(): Int {
        var value = this.current
        this.current = this.current + 1
        return value
    }
}

for (i in IntRange(0, 5)) {
    IO.println(i)  // 0, 1, 2, 3, 4
}
```

Because `IntRange` is iterable, it also works with everything in `@iter`:

```saffron
import "@iter" as Iter

var squares = Iter.map(IntRange(1, 5), fun (n: Int): Int => n * n)
// [1, 4, 9, 16]
```
