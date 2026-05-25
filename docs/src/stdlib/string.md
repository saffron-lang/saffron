# String Methods

Strings are immutable. All methods return new values.

## Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `s.length()` | `Int` | Number of characters |
| `s.split(delim)` | `List<String>` | Split by delimiter |
| `s.trim()` | `String` | Remove leading/trailing whitespace |
| `s.contains(sub)` | `Bool` | Check if substring exists |
| `s.starts_with(prefix)` | `Bool` | Check prefix |
| `s.ends_with(suffix)` | `Bool` | Check suffix |
| `s.replace(old, new)` | `String` | Replace all occurrences |
| `s.to_upper()` | `String` | Uppercase |
| `s.to_lower()` | `String` | Lowercase |
| `s.slice(start, end)` | `String` | Substring by index |
| `s.index_of(sub)` | `Int` | First index of substring (-1 if not found) |
| `s.repeat(n)` | `String` | Repeat n times |
| `s.to_number()` | `Int \| Float \| Nil` | Parse as number |

## Examples

```saffron
"hello world".split(" ")     // ["hello", "world"]
"  hi  ".trim()              // "hi"
"hello".contains("ell")      // true
"hello".starts_with("he")    // true
"hello".replace("l", "L")    // "heLLo"
"abc".to_upper()             // "ABC"
"hello".slice(1, 3)          // "el"
"hello".index_of("lo")       // 3
"ha".repeat(3)               // "hahaha"
"123".to_number()            // 123
```

## String interpolation

Double-quoted strings support `${}` interpolation:

```saffron
var name = "world"
"hello ${name}"           // "hello world"
"${1 + 2} is three"      // "3 is three"
```
