# Iterators and Pipes

## The iterator protocol

Any object that implements these three methods is iterable:

- `.iter()` — returns an iterator object
- `.next?()` — returns `true` if there's a next element
- `.next()` — returns the next element

Lists, maps, strings, and ranges all implement this protocol.

## Manual iteration

```saffron
var list = [10, 20, 30]
var iter = list.iter()

while (iter.next?()) {
    IO.println(iter.next())
}
```

## For-in loops

`for-in` is syntactic sugar for the iterator protocol:

```saffron
for (item in [10, 20, 30]) {
    IO.println(item)
}
```

## The `@iter` module

The standard library provides functional iteration utilities:

```saffron
import "@iter" as Iter

var numbers = [1, 2, 3, 4, 5]

var doubled = Iter.map(numbers, fun (x: Number): Number => x * 2)
// [2, 4, 6, 8, 10]

var evens = Iter.filter(numbers, fun (x: Number): Bool => x % 2 == 0)
// [2, 4]

var total = Iter.reduce(numbers, fun (acc: Number, x: Number): Number => acc + x, 0)
// 15
```

Other functions: `any`, `all`, `each`, `count`, `find`, `flat_map`, `zip`, `enumerate`, `take`, `skip`, `sum`.

## Named imports

Import specific functions directly:

```saffron
import { map, filter, reduce } from "@iter"

var result = filter([1, 2, 3, 4], fun (x: Number): Bool => x > 2)
// [3, 4]
```

## The pipe operator

The pipe operator `|>` passes the left-hand value as the first argument to the right-hand function call:

```saffron
import { map, filter } from "@iter"

var result = [1, 2, 3, 4, 5]
    |> filter(fun (x: Number): Bool => x % 2 == 0)
    |> map(fun (x: Number): Number => x * 10)

IO.println(result)  // [20, 40]
```

Pipes make chains of transformations read top-to-bottom instead of inside-out:

```saffron
// Without pipes (hard to read):
IO.println(map(filter([1,2,3,4,5], fun (x: Number): Bool => x > 2), fun (x: Number): Number => x * x))

// With pipes (clear):
[1, 2, 3, 4, 5]
    |> filter(fun (x: Number): Bool => x > 2)
    |> map(fun (x: Number): Number => x * x)
    |> IO.println()
```

## Writing iterable classes

Make your own class iterable by implementing the protocol:

```saffron
class Range {
    var start: Number
    var end: Number

    fun init(start: Number, end: Number) {
        this.start = start
        this.end = end
    }

    fun iter(): RangeIterator {
        return RangeIterator(this.start, this.end)
    }
}

class RangeIterator {
    var current: Number
    var end: Number

    fun init(start: Number, end: Number) {
        this.current = start
        this.end = end
    }

    fun next?(): Bool {
        return this.current < this.end
    }

    fun next(): Number {
        var value = this.current
        this.current = this.current + 1
        return value
    }
}

for (i in Range(0, 5)) {
    IO.println(i)  // 0, 1, 2, 3, 4
}
```
