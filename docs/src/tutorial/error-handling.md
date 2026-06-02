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

## Catching runtime errors

Runtime errors (index out of bounds, nil access, etc.) are catchable:

```saffron
try {
    var list = [1, 2, 3]
    list[99]
} catch (e) {
    IO.println("caught: ${e}")  // caught: Index 99 out of bounds...
}
```

## Typed catch clauses

Catch specific error types:

```saffron
class HttpError {
    var code: Number
    var message: String
    fun init(code: Number, message: String) {
        this.code = code
        this.message = message
    }
}

try {
    throw HttpError(404, "Not Found")
} catch (e: HttpError) {
    IO.println("HTTP ${e.code}: ${e.message}")
} catch (e) {
    IO.println("Unknown error: ${e}")
}
```

## Using Result for recoverable errors

For functions that can fail without exceptional circumstances, prefer the `Result` enum pattern:

```saffron
enum Result<T, E> {
    Ok(value: T),
    Err(error: E)
}

fun parse_int(s: String): Result<Number, String> {
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
