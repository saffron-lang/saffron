# Iter

```saffron
import "@iter" as Iter
// or
import { map, filter, reduce } from "@iter"
```

Functional utilities for working with iterables — lists, maps, strings, or any type with an `.iter()` method returning an object that has `has_next()` and `next()`.

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

## Sorting and grouping

### `sort_by(list, key_func) -> List`

Sort a list by a key function:

```saffron
Iter.sort_by(["banana", "apple", "fig"], fun (s: String): Number => s.length())
// ["fig", "apple", "banana"]
```

### `group_by(list, key_func) -> Map`

Group elements by a key function:

```saffron
Iter.group_by([1, 2, 3, 4, 5], fun (x: Number): String => if (x % 2 == 0) { "even" } else { "odd" })
// {"odd": [1, 3, 5], "even": [2, 4]}
```

### `frequencies(list) -> Map`

Count occurrences of each element:

```saffron
Iter.frequencies(["a", "b", "a", "c", "a"])
// {"a": 3, "b": 1, "c": 1}
```

### `unique(list) -> List`

Remove duplicates (preserving first occurrence order):

```saffron
Iter.unique([1, 2, 3, 2, 1])  // [1, 2, 3]
```

## Additional utilities

### `drop(list, n) -> List`

Alias for `skip`.

### `chunk(list, size) -> List`

Split a list into chunks of the given size:

```saffron
Iter.chunk([1, 2, 3, 4, 5], 2)
// [[1, 2], [3, 4], [5]]
```

### `zip_with(a, b, func) -> List`

Combine two lists element-wise using a function:

```saffron
Iter.zip_with([1, 2, 3], [10, 20, 30], fun (a: Number, b: Number): Number => a + b)
// [11, 22, 33]
```

### `max(list)` / `min(list)`

Return the maximum/minimum element:

```saffron
Iter.max([3, 1, 4, 1, 5])  // 5
Iter.min([3, 1, 4, 1, 5])  // 1
```

### `reverse(list) -> List`

Return a reversed copy:

```saffron
Iter.reverse([1, 2, 3])  // [3, 2, 1]
```

### `join(list, sep) -> String`

Join elements into a string with a separator:

```saffron
Iter.join(["a", "b", "c"], ", ")  // "a, b, c"
```
