# Generics

Generics let you write code that works with any type while maintaining type safety.

## Generic functions

```saffron
fun first<T>(list: List<T>): T {
    return list[0]
}

IO.println(first([1, 2, 3]))       // 1
IO.println(first(["a", "b"]))      // a
```

## Generic classes

```saffron
class Pair<A, B> {
    var first: A
    var second: B

    fun init(first: A, second: B) {
        this.first = first
        this.second = second
    }

    fun swap(): Pair<B, A> {
        return Pair(this.second, this.first)
    }
}

var p = Pair(42, "hello")
IO.println(p.first)   // 42
IO.println(p.second)  // hello

var swapped = p.swap()
IO.println(swapped.first)  // hello
```

## Generic enums

```saffron
enum Option<T> {
    Some(value: T),
    None
}

enum Result<T, E> {
    Ok(value: T),
    Err(error: E)
}

var maybe: Option<Int> = Option.Some(42)
var result: Result<String, String> = Result.Ok("success")
```

## Type inference with generics

In most cases, generic type parameters are inferred:

```saffron
var list = [1, 2, 3]           // List<Int>
var pair = Pair("hi", true)    // Pair<String, Bool>
var opt = Option.Some(3.14)    // Option<Float>
```

## Multiple type parameters

```saffron
fun zip_with<A, B, C>(a: List<A>, b: List<B>, f: (A, B) => C): List<C> {
    var result: List<C> = []
    var len = Math.min(a.length(), b.length())
    for (var i = 0; i < len; i = i + 1) {
        result.push(f(a[i], b[i]))
    }
    return result
}

var sums = zip_with([1, 2, 3], [4, 5, 6], fun (a: Int, b: Int): Int => a + b)
IO.println(sums)  // [5, 7, 9]
```
