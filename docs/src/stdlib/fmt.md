# Fmt

```saffron
import "@fmt" as Fmt
```

String formatting utilities.

## Functions

### `Fmt.format(template: String, args...): String`

Format a string with positional placeholders:

```saffron
Fmt.format("{} + {} = {}", 1, 2, 3)  // "1 + 2 = 3"
```

### `Fmt.pad_left(s: String, width: Int, char: String): String`

Pad a string on the left:

```saffron
Fmt.pad_left("42", 5, "0")  // "00042"
```

### `Fmt.pad_right(s: String, width: Int, char: String): String`

Pad a string on the right:

```saffron
Fmt.pad_right("hi", 5, ".")  // "hi..."
```
