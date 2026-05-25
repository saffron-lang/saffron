# Reflect

```saffron
import "@reflect" as Reflect
```

Runtime type introspection.

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Reflect.type_of(value)` | `String` | Runtime type name |
| `Reflect.fields(instance)` | `List<String>` | Field names of a class instance |
| `Reflect.methods(value)` | `List<String>` | Method names |
| `Reflect.doc(value)` | `String \| Nil` | Docstring (if defined with `///`) |

## Example

```saffron
import "@reflect" as Reflect

class Point {
    var x: Float
    var y: Float
    fun init(x: Float, y: Float) {
        this.x = x
        this.y = y
    }
}

var p = Point(1.0, 2.0)
IO.println(Reflect.type_of(p))    // "Point"
IO.println(Reflect.fields(p))     // ["x", "y"]
IO.println(Reflect.methods(p))    // ["init"]
```

## Docstrings

Functions and classes documented with `///` comments expose their docs at runtime:

```saffron
/// Compute the square of a number.
fun square(x: Float): Float {
    return x * x
}

IO.println(Reflect.doc(square))  // "Compute the square of a number."
```
