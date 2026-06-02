# Path

```saffron
import "@path" as Path
```

File path manipulation utilities.

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Path.join(parts)` | `String` | Join a list of path segments |
| `Path.dirname(path)` | `String` | Parent directory |
| `Path.basename(path)` | `String` | File name component |
| `Path.ext(path)` | `String` | File extension (with dot) |
| `Path.stem(path)` | `String` | File name without extension |
| `Path.is_absolute(path)` | `Bool` | Check if path is absolute |
| `Path.normalize(path)` | `String` | Resolve `.` and `..` segments |
| `Path.resolve(parts)` | `String` | Resolve to absolute path |
| `Path.relative(from, to)` | `String` | Compute relative path between two paths |
| `Path.sep()` | `String` | Platform path separator |

## Example

```saffron
import "@path" as Path

var file = Path.join(["/home", "user", "docs", "readme.md"])
IO.println(file)                  // /home/user/docs/readme.md
IO.println(Path.dirname(file))    // /home/user/docs
IO.println(Path.basename(file))   // readme.md
IO.println(Path.ext(file))        // .md
IO.println(Path.stem(file))       // readme
IO.println(Path.is_absolute(file))  // true

// Normalize removes redundant separators and resolves ..
IO.println(Path.normalize("/home/user/../admin/./docs"))
// /home/admin/docs
```
