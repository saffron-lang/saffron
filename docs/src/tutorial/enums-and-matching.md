# Enums and Pattern Matching

## Defining enums

Enums define a type that can be one of several variants. Variants can carry associated data:

```saffron
enum Direction {
    North,
    South,
    East,
    West
}

enum Option<T> {
    Some(value: T),
    None
}

enum Result<T, E> {
    Ok(value: T),
    Err(error: E)
}
```

## Creating enum values

```saffron
var dir = Direction.North
var maybe = Option.Some(42)
var nothing: Option<Number> = Option.None
```

## Pattern matching with `match`

`match` destructures enum variants and binds their fields:

```saffron
var result: Option<Number> = Option.Some(42)

var message = match (result) {
    Some(v) => "Got ${v}",
    None => "Nothing"
}

IO.println(message)  // Got 42
```

## Exhaustive matching

The compiler ensures you handle all variants. This is a compile error:

```saffron
// Error: non-exhaustive match — missing variant 'West'
var name = match (dir) {
    North => "north",
    South => "south",
    East => "east"
}
```

## Nested patterns

Match on nested enum structures:

```saffron
enum Expr {
    Num(value: Number),
    Add(left: Expr, right: Expr),
    Mul(left: Expr, right: Expr)
}

fun eval(expr: Expr): Number {
    return match (expr) {
        Num(v) => v,
        Add(l, r) => eval(l) + eval(r),
        Mul(l, r) => eval(l) * eval(r)
    }
}

var expr = Expr.Add(Expr.Num(1.0), Expr.Mul(Expr.Num(2.0), Expr.Num(3.0)))
IO.println(eval(expr))  // 7.0
```

## Type patterns with `is`

Match on the type of a value:

```saffron
class Dog { fun init() {} }
class Cat { fun init() {} }

fun describe(animal): String {
    return match (animal) {
        is Dog(d) => "a dog",
        is Cat(c) => "a cat"
    }
}
```

## Destructuring enum values

Outside of `match`, use `let` to destructure a known variant:

```saffron
let Some(value) = Option.Some(42)
IO.println(value)  // 42
```
