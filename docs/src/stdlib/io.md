# IO

The `IO` module is available globally -- no import needed.

## Functions

### `IO.println(value)`

Prints a value followed by a newline.

```saffron
IO.println("hello")    // hello
IO.println(42)         // 42
IO.println([1, 2, 3])  // [1, 2, 3]
```

### `IO.print(value)`

Prints a value without a trailing newline.

```saffron
IO.print("loading...")
```

### `IO.read_file(path: String): String`

Reads an entire file as a string.

```saffron
var contents = IO.read_file("data.txt")
IO.println(contents)
```

### `IO.write_file(path: String, content: String)`

Writes a string to a file, creating it if it doesn't exist.

```saffron
IO.write_file("output.txt", "hello world")
```

### `IO.file_exists(path: String): Bool`

Check if a file exists.

```saffron
if (IO.file_exists("config.json")) {
    var config = IO.read_file("config.json")
}
```

### `IO.append_file(path: String, content: String)`

Append content to a file, creating it if it doesn't exist.

```saffron
IO.append_file("log.txt", "new entry\n")
```

### `IO.mkdir(path: String)`

Create a directory (and parents) at the given path.

```saffron
IO.mkdir("output/reports")
```

### `IO.list_dir(path: String): List<String>`

List the contents of a directory recursively.

```saffron
var files = IO.list_dir("src")
for (f in files) {
    IO.println(f)
}
```

### `IO.walk_dir(path: String): List<String>`

Alias for `list_dir`.

## File class

For incremental file I/O (reading line-by-line, writing in chunks), use `IO.open()`:

```saffron
var f = IO.open("data.txt", "r")
var line = f.read_line()
while (line != "") {
    IO.print(line)
    line = f.read_line()
}
f.close()
```

### `IO.open(path: String, mode: String): File`

Open a file. Modes: `"r"`, `"w"`, `"a"`, `"rb"`, `"wb"`, `"ab"`, `"r+"`, `"w+"`.

### File methods

| Method | Returns | Description |
|--------|---------|-------------|
| `f.read(max_bytes)` | `String` | Read up to max_bytes (returns `""` at EOF) |
| `f.read_line()` | `String` | Read one line (returns `""` at EOF) |
| `f.read_all()` | `String` | Read remaining content |
| `f.write(data)` | -- | Write a string |
| `f.write_line(data)` | -- | Write a string + newline |
| `f.seek(offset)` | -- | Seek to byte offset from start |
| `f.seek_end(offset)` | -- | Seek from end |
| `f.tell()` | `Int` | Current byte offset |
| `f.rewind()` | -- | Seek to beginning |
| `f.flush()` | -- | Flush buffered data |
| `f.close()` | -- | Close the file handle |
| `f.size()` | `Int` | File size in bytes |
| `f.eof()` | `Bool` | True if at end of file |
| `f.is_open()` | `Bool` | True if file is still open |
| `f.path()` | `String` | Path the file was opened with |
| `f.mode()` | `String` | Mode the file was opened with |

## Notes

- `IO.println` can print any value type -- numbers, booleans, lists, maps, and class instances all have a string representation
- File paths are relative to the working directory where the program was launched
