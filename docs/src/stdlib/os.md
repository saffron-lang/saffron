# OS

```saffron
import "@os" as OS
```

Operating system interaction.

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `OS.args()` | `List<String>` | Command-line arguments |
| `OS.env(name)` | `String \| Nil` | Get environment variable |
| `OS.exit(code)` | — | Exit the process |
| `OS.cwd()` | `String` | Current working directory |

## Example

```saffron
import "@os" as OS

var args = OS.args()
if (args.length() < 2) {
    IO.println("Usage: saffron script.sf <name>")
    OS.exit(1)
}

IO.println("Hello, ${args[1]}!")
```
