# IO

The `IO` module is available globally — no import needed.

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
IO.print("enter name: ")
```

### `IO.readln(): String`

Reads a line of input from stdin.

```saffron
IO.print("Name: ")
var name = IO.readln()
IO.println("Hello, ${name}!")
```

### `IO.read_file(path: String): String`

Reads an entire file as a string.

```saffron
var contents = IO.read_file("data.txt")
```

### `IO.write_file(path: String, content: String)`

Writes a string to a file, creating it if it doesn't exist.

```saffron
IO.write_file("output.txt", "hello world")
```
