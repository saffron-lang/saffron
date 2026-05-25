# The Saffron Programming Language

Saffron is a statically typed scripting language that compiles to bytecode and runs on a stack-based virtual machine. It's designed to be expressive, fast to write, and safe by default.

## Features

- **Static typing with inference** — catch errors at compile time without writing types everywhere
- **Pattern matching** — enums with associated data and exhaustive `match` expressions
- **Cooperative async** — lightweight tasks with `spawn`, `await`, and channels
- **First-class functions** — lambdas, closures, and higher-order functions
- **Iterators and pipes** — composable data transformations with `|>`
- **Classes and interfaces** — single inheritance with multiple interface conformance
- **Garbage collected** — mark-sweep GC so you never manage memory manually

## A taste of Saffron

```saffron
import "@iter" as Iter

enum Shape {
    Circle(radius: Number),
    Rect(width: Number, height: Number)
}

fun area(shape: Shape): Number {
    return match (shape) {
        Circle(r) => 3.14159 * r * r,
        Rect(w, h) => w * h
    }
}

var shapes = [Shape.Circle(5), Shape.Rect(3, 4)]
var areas = Iter.map(shapes, area)
IO.println(areas)  // [78.5398, 12]
```

## Who is this for?

Saffron is a good fit if you want a language that feels like Python or TypeScript to write but catches type errors before your code runs. It's a single binary with no dependencies — download it and start writing.

## Next steps

Head to [Installation](./getting-started/installation.md) to get started.
