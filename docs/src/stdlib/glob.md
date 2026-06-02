# Glob

```saffron
import "@glob" as Glob
```

File glob pattern matching and file discovery.

## Pattern Syntax

| Pattern | Matches |
|---------|---------|
| `*` | Any sequence of characters (not `/`) |
| `**` | Any path segment(s), including nested directories |
| `?` | Any single character |
| `[abc]` | One of the listed characters |
| `[!abc]` | Any character NOT in the list |

## Functions

### `Glob.find(pattern: String): List<String>`

Find files matching a glob pattern relative to the current directory:

```saffron
var sf_files = Glob.find("src/**/*.sf")
IO.println(sf_files)  // ["src/lib/math.sf", "src/lib/iter.sf", ...]
```

### `Glob.find_in(base: String, pattern: String): List<String>`

Find files matching a pattern relative to a base directory:

```saffron
var tests = Glob.find_in("test", "*.sf")
// ["test/async.sf", "test/classes.sf", ...]
```

### `Glob.matches(pattern: String, text: String): Bool`

Test if a string matches a glob pattern:

```saffron
Glob.matches("*.sf", "main.sf")           // true
Glob.matches("*.sf", "src/main.sf")       // false (no path separator match for *)
Glob.matches("src/**/*.sf", "src/a/b.sf") // true
```

### `Glob.is_ignored(path: String, patterns: List<String>): Bool`

Check if a path would be ignored by a list of gitignore-style patterns:

```saffron
var ignores = ["*.tmp", "build/", "node_modules/"]
Glob.is_ignored("output.tmp", ignores)       // true
Glob.is_ignored("src/main.sf", ignores)      // false
```

## Example

```saffron
import "@glob" as Glob
import "@iter" as Iter

// Find all Saffron source files and count them
var files = Glob.find("src/**/*.sf")
IO.println("Found ${files.length()} source files")

// Filter for test files only
var tests = Iter.filter(files, fun (f: String): Bool => f.contains("test"))
IO.println("${tests.length()} test files")
```
