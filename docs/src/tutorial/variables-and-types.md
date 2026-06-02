# Variables and Types

## Declaring variables

Use `var` to declare a mutable variable:

```saffron
var x: Number = 5
var name: String = "saffron"
```

## Type inference

When the type is obvious from the initializer, you can omit the annotation:

```saffron
var x = 5          // inferred as Number
var pi = 3.14      // inferred as Number
var name = "hi"    // inferred as String
var flag = true    // inferred as Bool
```

## Primitive types

| Type | Description | Examples |
|------|-------------|----------|
| `Number` | 64-bit floating-point number | `42`, `3.14`, `-1`, `0` |
| `String` | UTF-8 text | `"hello"`, `""` |
| `Bool` | Boolean | `true`, `false` |
| `Nil` | Absence of value | `nil` |

Saffron uses a single `Number` type for all numeric values (both integers and floating-point). The LLVM compiler backend also accepts `Int` and `Float` as type annotations, which are treated as distinct types internally for optimization, but semantically equivalent to `Number` in most code.

## Type checking

Saffron validates types at compile time. This won't compile:

```saffron
var x: Number = "hello"  // Error: expected Number, got String
```

## Methods on primitives

Numbers and Booleans have methods:

```saffron
(-5).abs()       // 5
(3.7).floor()    // 3
(3.2).ceil()     // 4
true.to_string() // "true"
```

## Type checking at runtime

The `is` operator checks a value's type:

```saffron
42 is Number      // true
"hi" is String    // true
nil is Nil        // true
```

## Destructuring

You can destructure lists in variable declarations:

```saffron
let [a, b, c] = [1, 2, 3]
// a = 1, b = 2, c = 3
```

With a splat to capture the middle:

```saffron
let [head, *middle, tail] = [1, 2, 3, 4, 5]
// head = 1, middle = [2, 3, 4], tail = 5
```

See the [Destructuring](../reference/destructuring.md) reference for the full syntax.
