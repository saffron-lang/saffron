# Functions

## Declaring functions

Functions are declared with `fun`, with type annotations on parameters and return type:

```saffron
fun add(a: Number, b: Number): Number {
    return a + b
}

IO.println(add(2, 3))  // 5
```

## Return type inference

If the return type is omitted, it's inferred from the function body:

```saffron
fun double(x: Number) {
    return x * 2
}
```

## Lambdas

Anonymous functions use the `fun` keyword with `=>` for single-expression bodies:

```saffron
var double = fun (x: Number): Number => x * 2
IO.println(double(5))  // 10
```

Multi-line lambdas use a block body:

```saffron
var greet = fun (name: String): String {
    return "Hello, ${name}!"
}
```

## First-class functions

Functions are values. Pass them as arguments, return them, store them in variables:

```saffron
fun apply(f: (Number) => Number, x: Number): Number {
    return f(x)
}

IO.println(apply(fun (n: Number): Number => n * n, 4))  // 16
```

## Closures

Functions capture variables from their enclosing scope:

```saffron
fun counter(): () => Number {
    var count = 0
    return fun (): Number {
        count = count + 1
        return count
    }
}

var next = counter()
IO.println(next())  // 1
IO.println(next())  // 2
IO.println(next())  // 3
```

## Generic functions

Functions can be generic over types:

```saffron
fun identity<T>(x: T): T {
    return x
}

IO.println(identity(42))       // 42
IO.println(identity("hello"))  // hello
```
