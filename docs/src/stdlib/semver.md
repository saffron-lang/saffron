# Semver

```saffron
import "@semver" as SemVer
```

Semantic Versioning 2.0 -- parse, compare, and match version constraints.

## Version class

```saffron
class Version {
    var major: Int
    var minor: Int
    var patch: Int
    var prerelease: String
    var build: String
}
```

## Functions

### Parsing

| Function | Returns | Description |
|----------|---------|-------------|
| `SemVer.parse(str)` | `Version` | Parse a semver string |
| `SemVer.is_valid(str)` | `Bool` | Check if string is valid semver |
| `SemVer.to_string(v)` | `String` | Format a Version back to string |

### Comparison

| Function | Returns | Description |
|----------|---------|-------------|
| `SemVer.compare(a, b)` | `Int` | -1, 0, or 1 |
| `SemVer.gt(a, b)` | `Bool` | a > b |
| `SemVer.lt(a, b)` | `Bool` | a < b |
| `SemVer.eq(a, b)` | `Bool` | a == b |
| `SemVer.gte(a, b)` | `Bool` | a >= b |
| `SemVer.lte(a, b)` | `Bool` | a <= b |

### Constraints

| Function | Returns | Description |
|----------|---------|-------------|
| `SemVer.satisfies(version, constraint)` | `Bool` | Check if version matches constraint |
| `SemVer.max_satisfying(versions, constraint)` | `Version` | Highest version satisfying constraint |

### Manipulation

| Function | Returns | Description |
|----------|---------|-------------|
| `SemVer.increment(v, level)` | `Version` | Bump "major", "minor", or "patch" |

## Constraint syntax

- `^1.2.3` -- compatible with (same major): `>=1.2.3, <2.0.0`
- `~1.2.3` -- approximately (same minor): `>=1.2.3, <1.3.0`
- `>=1.0.0` -- greater than or equal
- `<2.0.0` -- less than
- `1.2.3` -- exact match

## Example

```saffron
import "@semver" as SemVer

var v = SemVer.parse("1.4.2-beta.1+build.789")
IO.println(v.major)       // 1
IO.println(v.prerelease)  // "beta.1"

var w = SemVer.parse("1.4.2")
IO.println(SemVer.gt(w, v))  // true (release > prerelease)

IO.println(SemVer.satisfies(w, "^1.0.0"))  // true
IO.println(SemVer.satisfies(w, "~1.4.0"))  // true
IO.println(SemVer.satisfies(w, ">=2.0.0")) // false

var next = SemVer.increment(w, "minor")
IO.println(SemVer.to_string(next))  // "1.5.0"
```
