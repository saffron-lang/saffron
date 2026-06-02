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
| `Test.assert_neq(actual, expected, message)` | Fail if values are equal |
| `Test.assert_gt(actual, expected, message)` | Fail if actual is not greater than expected |
| `Test.assert_lt(actual, expected, message)` | Fail if actual is not less than expected |
| `Test.assert_contains(haystack, needle, message)` | Fail if string does not contain substring |
| `Test.section(name)` | Print a section heading for grouping tests |
| `Test.summary()` | Print results summary |
| `Test.run_all()` | Run all registered test functions and report |

## Basic Usage

```saffron
import "@test" as Test

Test.assert_eq(1 + 1, 2, "basic addition")
Test.assert_eq("hello".length(), 5, "string length")
Test.assert([1, 2, 3].contains(2), "list contains")
Test.assert_neq(true, false, "booleans differ")

Test.summary()
```

Output:

```
  All 4 assertions passed
```

## Decorator-Based Tests

Use the `@Test.test` decorator to register functions as test cases, then call `Test.run_all()`:

```saffron
import "@test" as Test

@Test.test
fun test_addition() {
    Test.assert_eq(1 + 1, 2, "basic addition")
    Test.assert_eq(2 + 2, 4, "more addition")
}

@Test.test
fun test_strings() {
    Test.assert_eq("hello".length(), 5, "string length")
    Test.assert_contains("hello world", "world", "substring")
}

Test.run_all()
```

Output:

```
Running 2 tests

  test_addition ... ok
  test_strings ... ok

Results: 4 passed, 0 failed, 4 total
```

## Sections

Use `Test.section()` to group assertions visually:

```saffron
import "@test" as Test

Test.section("Math")
Test.assert_eq(2 * 3, 6, "multiplication")
Test.assert_gt(5, 3, "greater than")

Test.section("Strings")
Test.assert_eq("hi".to_upper(), "HI", "to_upper")

Test.summary()
```

## Writing test files

By convention, test files are named `test_*.sf` or placed in a `test/` directory:

```saffron
// test/test_math.sf
import "@test" as Test
import "@math" as Math

Test.assert_eq(Math.abs(-5), 5, "abs negative")
Test.assert_eq(Math.max(3, 7), 7, "max")

Test.summary()
```

Run with:

```bash
tools/saffron run test/test_math.sf
```
