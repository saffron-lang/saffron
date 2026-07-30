# Trailing Closures

Trailing closures allow you to pass a function body after a call expression, making builder-style and callback-heavy code more readable.

## Syntax

When the last argument to a function is a closure, you can write it outside the parentheses:

```saffron
// Standard call:
each(items, fun (item: String) {
    IO.println(item)
})

// With trailing closure:
each(items) { item =>
    IO.println(item)
}
```

## Rules

1. The trailing block `{ ... }` becomes the last argument to the preceding function call
2. If the function takes no other arguments, parentheses can be omitted entirely
3. Parameters are specified before `=>` inside the block

## No-argument closures

If the closure takes no parameters, omit the `=>`:

```saffron
// Equivalent to: spawn(fun () { IO.println("hello") })
spawn {
    IO.println("hello")
}
```

## With arguments and trailing closure

When the function takes other arguments plus a trailing closure:

```saffron
// With parens for first arguments, closure after:
button([on_click(handler)]) {
    text("Click me")
}

// This is equivalent to:
button([on_click(handler)], fun () {
    text("Click me")
})
```

## Parameterized trailing closures

Specify parameters before `=>`:

```saffron
each(items) { item =>
    IO.println(item)
}

map(numbers) { n =>
    n * 2
}
```

## Paren-free style

If the only argument is the trailing closure, no parentheses are needed:

```saffron
div {
    h1 { text("Hello") }
    p { text("World") }
}

// Equivalent to:
div(fun () {
    h1(fun () { text("Hello") })
    p(fun () { text("World") })
})
```

This is the basis of Turmeric's element builder DSL.

## Real-world example

```saffron
import "@iter" as Iter

var evens = Iter.filter([1, 2, 3, 4, 5]) { n => n % 2 == 0 }
var result = Iter.map(evens) { n => n * 10 }

IO.println(result)  // [20, 40]
```

## Notes

- Trailing closures are syntactic sugar — they compile to the same code as passing a lambda
- The return value of the last expression in a trailing closure is the closure's return value
- A trailing closure captures its enclosing scope like any other lambda
