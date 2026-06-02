# Iter

```saffron
import "@iter" as Iter
// or
import { map, filter, reduce } from "@iter"
```

Functional utilities for working with iterables (lists, maps, strings, or any type implementing the iterator protocol).

## Transforming

### `map(iterable, func) -> List`

Apply a function to each element:

```saffron
Iter.map([1, 2, 3], fun (x: Number): Number => x * 2)
// [2, 4, 6]
```

### `filter(iterable, func) -> List`

Keep elements where the function returns true:

```saffron
Iter.filter([1, 2, 3, 4], fun (x: Number): Bool => x % 2 == 0)
// [2, 4]
```

### `flat_map(iterable, func) -> List`

Map and flatten one level:

```saffron
Iter.flat_map([[1, 2], [3, 4]], fun (x: List<Number>): List<Number> => x)
// [1, 2, 3, 4]
```

## Reducing

### `reduce(iterable, func, initial)`

Fold elements into a single value:

```saffron
Iter.reduce([1, 2, 3, 4], fun (acc: Number, x: Number): Number => acc + x, 0)
// 10
```

### `sum(iterable) -> Number`

Sum all elements:

```saffron
Iter.sum([1, 2, 3])  // 6
```

### `count(iterable, func) -> Number`

Count elements matching a predicate:

```saffron
Iter.count([1, 2, 3, 4], fun (x: Number): Bool => x > 2)
// 2
```

## Searching

### `find(iterable, func)`

Return the first element matching a predicate (or nil):

```saffron
Iter.find([1, 2, 3], fun (x: Number): Bool => x > 1)
// 2
```

### `any(iterable, func) -> Bool`

True if any element matches:

```saffron
Iter.any([1, 2, 3], fun (x: Number): Bool => x > 2)  // true
```

### `all(iterable, func) -> Bool`

True if all elements match:

```saffron
Iter.all([2, 4, 6], fun (x: Number): Bool => x % 2 == 0)  // true
```

## Iteration

### `each(iterable, func)`

Execute a function for each element (for side effects):

```saffron
Iter.each(["a", "b", "c"], IO.println)
```

## Slicing

### `take(iterable, n) -> List`

Take the first n elements:

```saffron
Iter.take([1, 2, 3, 4, 5], 3)  // [1, 2, 3]
```

### `skip(iterable, n) -> List`

Skip the first n elements:

```saffron
Iter.skip([1, 2, 3, 4, 5], 2)  // [3, 4, 5]
```

## Combining

### `zip(a, b) -> List`

Pair elements from two iterables:

```saffron
Iter.zip([1, 2, 3], ["a", "b", "c"])
// [[1, "a"], [2, "b"], [3, "c"]]
```

### `enumerate(iterable) -> List`

Pair each element with its index:

```saffron
Iter.enumerate(["a", "b", "c"])
// [[0, "a"], [1, "b"], [2, "c"]]
```
