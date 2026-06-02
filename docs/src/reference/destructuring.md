# Destructuring

Destructuring lets you unpack values from lists and enums into individual variables.

## List destructuring

```saffron
let [a, b, c] = [1, 2, 3]
// a = 1, b = 2, c = 3
```

## Splat (rest) syntax

Capture remaining elements with `*`:

```saffron
let [head, *rest] = [1, 2, 3, 4, 5]
// head = 1, rest = [2, 3, 4, 5]

let [first, *middle, last] = [1, 2, 3, 4, 5]
// first = 1, middle = [2, 3, 4], last = 5

let [*init, tail] = [1, 2, 3, 4, 5]
// init = [1, 2, 3, 4], tail = 5
```

## Enum destructuring

Extract fields from a known variant:

```saffron
enum Point {
    XY(x: Number, y: Number)
}

let XY(x, y) = Point.XY(3.0, 4.0)
IO.println(x)  // 3.0
IO.println(y)  // 4.0
```

Works with `Option`, `Result`, and any enum:

```saffron
enum Option<T> {
    Some(value: T),
    None
}

let Some(value) = Option.Some(42)
IO.println(value)  // 42
```

## In match arms

Destructuring is the primary mechanism in `match`:

```saffron
enum Expr {
    Num(value: Number),
    Add(left: Expr, right: Expr)
}

fun eval(expr: Expr): Number {
    return match (expr) {
        Num(v) => v,
        Add(l, r) => eval(l) + eval(r)
    }
}
```

## Nested destructuring

```saffron
let [[a, b], [c, d]] = [[1, 2], [3, 4]]
// a = 1, b = 2, c = 3, d = 4
```
