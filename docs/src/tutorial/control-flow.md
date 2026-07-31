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

`for-in` walks a collection by index, so it works over lists and strings:

```saffron
// Lists
for (item in [10, 20, 30]) {
    IO.println(item)
}

// Strings — character by character
for (ch in "abc") {
    IO.println(ch)  // a, b, c
}
```

Maps do **not** work — `for-in` over a Map segfaults (BUGS #62). Iterate
`keys()` instead:

```saffron
var m: Map<String, Int> = {"a": 1, "b": 2}
var ks: List<String> = m.keys()
for (k in ks) {
    IO.println(k)
}
```

Custom types cannot join in yet — see [Iterators](./iterators.md) for why and
for the workaround.

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
