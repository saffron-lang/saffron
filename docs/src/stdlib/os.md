# OS

```saffron
import "@os" as OS
```

Operating system interaction.

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `OS.args()` | `List<String>` | Command-line arguments |
| `OS.env(name)` | `String` | Get environment variable (empty string if not set) |
| `OS.cwd()` | `String` | Current working directory |
| `OS.platform()` | `String` | Operating system name (e.g., "darwin", "linux") |
| `OS.path_sep()` | `String` | Path separator ("/" on Unix, "\\" on Windows) |
| `OS.exec(cmd)` | `String` | Execute a shell command, return output |
| `OS.file_exists(path)` | `Bool` | Check if a file or directory exists |
| `OS.mkdir(path)` | -- | Create a directory |
| `OS.list_dir(path)` | `List<String>` | List directory contents |

## Example

```saffron
import "@os" as OS

var args = OS.args()
if (args.length() < 2) {
    IO.println("Usage: saffron script.sf <name>")
}

IO.println("Platform: ${OS.platform()}")
IO.println("CWD: ${OS.cwd()}")

// Environment variables
var home = OS.env("HOME")
IO.println("Home: ${home}")

// List files in current directory
var files = OS.list_dir(".")
for (f in files) {
    IO.println(f)
}

// Execute a shell command
var output = OS.exec("echo hello")
IO.println(output)  // "hello\n"
```

### `OS.exit(code)`

Exit the process with the given status code:

```saffron
if (error_occurred) {
    OS.exit(1)
}
```
