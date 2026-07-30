# The Saffron Programming Language

Saffron is a statically typed scripting language designed to be expressive, fast to write, and safe by default. It is compiled by a self-hosted compiler — written in Saffron — that emits LLVM IR, targeting both native binaries and WebAssembly.

## Features

- **Static typing with inference** — catch errors at compile time without writing types everywhere
- **Pattern matching** — enums with associated data and exhaustive `match` expressions
- **Cooperative async** — lightweight tasks with `spawn`, `await`, and `sleep`
- **First-class functions** — lambdas, closures, trailing closures, and higher-order functions
- **Iterators** — a small `has_next()`/`next()` protocol that `for-in` and `@iter` build on
- **Classes and interfaces** — single inheritance with multiple interface conformance
- **FFI** — call C functions directly via `@extern` for system-level integration
- **Garbage collected** — mark-sweep GC so you never manage memory manually
- **Turmeric web framework** — reactive UI with signals, compiled to WASM

## A taste of Saffron

```saffron
import "@iter" as Iter

enum Shape {
    Circle(radius: Float),
    Rect(width: Float, height: Float)
}

fun area(shape: Shape): Float {
    return match (shape) {
        Circle(r) => 3.14159 * r * r,
        Rect(w, h) => w * h
    }
}

var shapes = [Shape.Circle(5.0), Shape.Rect(3.0, 4.0)]
var areas = Iter.map(shapes, area)
IO.println(areas)  // [78.5397, 12]
```

## Who is this for?

Saffron is a good fit if you want a language that feels like Python or TypeScript to write but catches type errors before your code runs. It's a single binary with no dependencies — download it and start writing.

## Next steps

Head to [Installation](./getting-started/installation.md) to get started.
