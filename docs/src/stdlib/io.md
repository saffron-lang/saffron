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

## Notes

- `IO.println` can print any value type -- numbers, booleans, lists, maps, and class instances all have a string representation
- File paths are relative to the working directory where the program was launched
