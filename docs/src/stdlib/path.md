# Path

```saffron
import "@path" as Path
```

File path manipulation utilities.

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Path.join(parts...)` | `String` | Join path segments |
| `Path.dirname(path)` | `String` | Parent directory |
| `Path.basename(path)` | `String` | File name component |
| `Path.extension(path)` | `String` | File extension (with dot) |
| `Path.is_absolute(path)` | `Bool` | Check if path is absolute |

## Example

```saffron
import "@path" as Path

var file = Path.join("/home", "user", "docs", "readme.md")
IO.println(file)                  // /home/user/docs/readme.md
IO.println(Path.dirname(file))    // /home/user/docs
IO.println(Path.basename(file))   // readme.md
IO.println(Path.extension(file))  // .md
```
