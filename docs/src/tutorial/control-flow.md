# Control Flow

## If / else

```saffron
var x = 10

if (x > 5) {
    IO.println("big")
} else if (x > 0) {
    IO.println("small")
} else {
    IO.println("zero or negative")
}
```

## While loops

```saffron
var i = 0
while (i < 5) {
    IO.println(i)
    i = i + 1
}
```

## C-style for loops

```saffron
for (var i = 0; i < 5; i = i + 1) {
    IO.println(i)
}
```

## For-in loops

Iterate over anything that implements the iterator protocol (`.iter()`, `.next?()`, `.next()`):

```saffron
for (item in [10, 20, 30]) {
    IO.println(item)
}
```

## Break and continue

Exit a loop early with `break`, or skip to the next iteration with `continue`:

```saffron
for (x in [1, 2, 3, 4, 5]) {
    if (x == 3) continue
    if (x == 5) break
    IO.println(x)
}
// prints: 1, 2, 4
```

## Match expressions

`match` compares a value against patterns and returns the result of the matching arm:

```saffron
var code = 404

var message = match (code) {
    200 => "OK",
    404 => "Not Found",
    500 => "Server Error"
}

IO.println(message)  // Not Found
```

Match is most powerful with enums — see [Enums and Pattern Matching](./enums-and-matching.md).
