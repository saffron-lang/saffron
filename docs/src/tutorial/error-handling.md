# Error Handling

## Try / catch / finally

Saffron uses exceptions for error handling:

```saffron
try {
    throw "something went wrong"
} catch (e) {
    IO.println("caught: ${e}")
} finally {
    IO.println("cleanup runs always")
}
```

## Throwing errors

Any value can be thrown:

```saffron
throw "file not found"
throw 404
throw {"code": 500, "message": "internal error"}
```

## Runtime errors are not catchable

`try`/`catch` handles values you `throw`. It does **not** handle runtime faults:
index-out-of-bounds, division by zero, and null-pointer errors are **fatal**.
They print to standard error and exit with status 1, without running any
`catch` or `finally` block.

```saffron
try {
    var list = [1, 2, 3]
    list[99]
} catch (e) {
    IO.println("caught: ${e}")  // never runs
}
// Output: Runtime Error: IndexError: index 99 out of bounds (length 3)
// Exit status: 1
```

To handle a potentially out-of-range index, check before indexing rather than
trying to recover afterwards:

```saffron
if (i >= 0 and i < list.length()) {
    IO.println(list[i])
} else {
    IO.println("index ${i} out of range")
}
```

Nil misuse is usually caught earlier still — the type checker rejects calling a
method on a nullable value, so it never becomes a runtime error at all.

## Discriminating error types

`catch` binds a single, untyped name — there is no type annotation on the binding. To handle
different error kinds differently, throw a class or enum and branch inside the catch block
with `is` or `match`:

```saffron
class HttpError {
    var code: Int
    var message: String
    fun init(code: Int, message: String) {
        this.code = code
        this.message = message
    }
}

try {
    throw HttpError(404, "Not Found")
} catch (e) {
    if (e is HttpError) {
        IO.println("HTTP ${e.code}: ${e.message}")
    } else {
        IO.println("Unknown error: ${e}")
    }
}
```

## Using Result for recoverable errors

For functions that can fail without exceptional circumstances, prefer the `Result` enum pattern:

```saffron
enum Result<T, E> {
    Ok(value: T),
    Err(error: E)
}

fun parse_int(s: String): Result<Int, String> {
    var n = s.to_number()
    if (n is Nil) {
        return Result.Err("not a number: ${s}")
    }
    return Result.Ok(n)
}

var result = parse_int("42")
var value = match (result) {
    Ok(v) => v,
    Err(e) => {
        IO.println("Error: ${e}")
        0
    }
}
```
