# Test

```saffron
import "@test" as T
```

A lightweight test framework for Saffron programs.

## Assertions

| Function | Description |
|----------|-------------|
| `T.assert(condition, message)` | Fail if condition is false |
| `T.assert_eq(actual, expected, message)` | Fail if values are not equal |
| `T.assert_ne(actual, expected, message)` | Fail if values are equal |
| `T.summary()` | Print results and exit with appropriate code |

## Example

```saffron
import "@test" as T

T.assert_eq(1 + 1, 2, "basic addition")
T.assert_eq("hello".length(), 5, "string length")
T.assert([1, 2, 3].contains(2), "list contains")
T.assert_ne(true, false, "booleans differ")

T.summary()
```

Output:

```
4 passed, 0 failed
```

## Writing test files

By convention, test files are named `test_*.sf` or placed in a `test/` directory:

```saffron
// test/test_math.sf
import "@test" as T
import "@math" as Math

T.assert_eq(Math.abs(-5), 5, "abs negative")
T.assert_eq(Math.max(3, 7), 7, "max")
T.assert(Math.is_close(Math.pi, 3.14159, 0.001), "pi approximation")

T.summary()
```

Run with:

```bash
saffron test/test_math.sf
```
