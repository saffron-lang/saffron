# Test

```saffron
import "@test" as Test
```

A lightweight test framework for Saffron programs.

## Assertions

| Function | Description |
|----------|-------------|
| `Test.assert(condition, message)` | Fail if condition is false |
| `Test.assert_eq(actual, expected, message)` | Fail if values are not equal |
| `Test.assert_ne(actual, expected, message)` | Fail if values are equal |
| `Test.summary()` | Print results and exit with appropriate code |

## Example

```saffron
import "@test" as Test

Test.assert_eq(1 + 1, 2, "basic addition")
Test.assert_eq("hello".length(), 5, "string length")
Test.assert([1, 2, 3].contains(2), "list contains")
Test.assert_ne(true, false, "booleans differ")

Test.summary()
```

Output:

```
4 passed, 0 failed
```

## Writing test files

By convention, test files are named `test_*.sf` or placed in a `test/` directory:

```saffron
// test/test_math.sf
import "@test" as Test
import "@math" as Math

Test.assert_eq(Math.abs(-5), 5, "abs negative")
Test.assert_eq(Math.max(3, 7), 7, "max")
Test.assert(Math.is_close(Math.pi, 3.14159, 0.001), "pi approximation")

Test.summary()
```

Run with:

```bash
saffron test/test_math.sf
```
