# Variables and Types

## Declaring variables

Use `var` to declare a mutable variable:

```saffron
var x: Int = 5
var name: String = "saffron"
```

## Type inference

When the type is obvious from the initializer, you can omit the annotation:

```saffron
var x = 5          // inferred as Int
var pi = 3.14      // inferred as Float
var name = "hi"    // inferred as String
var flag = true    // inferred as Bool
```

## Primitive types

| Type | Description | Examples |
|------|-------------|----------|
| `Int` | 64-bit signed integer | `42`, `-1`, `0` |
| `Float` | 64-bit floating point | `3.14`, `-0.5`, `1.0` |
| `String` | UTF-8 text | `"hello"`, `""` |
| `Bool` | Boolean | `true`, `false` |
| `Nil` | Absence of value | `nil` |

## Type checking

Saffron validates types at compile time. This won't compile:

```saffron
var x: Int = "hello"  // Error: expected Int, got String
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
42 is Int         // true
3.14 is Float     // true
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
