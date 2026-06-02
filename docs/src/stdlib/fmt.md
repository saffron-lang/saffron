# Fmt

```saffron
import "@fmt" as Fmt
```

String formatting utilities.

## Functions

### `Fmt.format(template: String, values: List<Any>): String`

Format a string with positional `{}` placeholders:

```saffron
Fmt.format("{} + {} = {}", [1, 2, 3])  // "1 + 2 = 3"
Fmt.format("Hello, {}!", ["world"])    // "Hello, world!"
```

### `Fmt.pad_left(text: String, width: Number, fill: String): String`

Pad a string on the left:

```saffron
Fmt.pad_left("42", 5, "0")  // "00042"
Fmt.pad_left("hi", 6, " ")  // "    hi"
```

### `Fmt.pad_right(text: String, width: Number, fill: String): String`

Pad a string on the right:

```saffron
Fmt.pad_right("hi", 5, ".")  // "hi..."
```

### `Fmt.center(text: String, width: Number, fill: String): String`

Center a string within the given width:

```saffron
Fmt.center("hi", 8, "=")  // "===hi==="
```

### `Fmt.repeat(text: String, n: Number): String`

Repeat a string n times:

```saffron
Fmt.repeat("ab", 3)  // "ababab"
Fmt.repeat("-", 20)  // "--------------------"
```
