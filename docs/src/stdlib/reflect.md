# Reflect

```saffron
import "@reflect" as Reflect
```

Runtime type introspection.

## Type checking

| Function | Returns | Description |
|----------|---------|-------------|
| `Reflect.type_of(value)` | `String` | Runtime type name |
| `Reflect.is_number(value)` | `Bool` | Check if value is a Number |
| `Reflect.is_string(value)` | `Bool` | Check if value is a String |
| `Reflect.is_bool(value)` | `Bool` | Check if value is a Bool |
| `Reflect.is_nil(value)` | `Bool` | Check if value is nil |
| `Reflect.is_list(value)` | `Bool` | Check if value is a List |
| `Reflect.is_map(value)` | `Bool` | Check if value is a Map |
| `Reflect.is_instance(value)` | `Bool` | Check if value is a class instance |
| `Reflect.is_class(value)` | `Bool` | Check if value is a class |

## Introspection

| Function | Returns | Description |
|----------|---------|-------------|
| `Reflect.fields(instance)` | `List<String>` | Field names of a class instance |
| `Reflect.field_types(instance)` | `Map<String, String>` | Field name to type mapping |
| `Reflect.class_name(instance)` | `String` | Name of the instance's class |
| `Reflect.doc(value)` | `String` | Docstring (if defined with `///`) |
| `Reflect.construct(class, args)` | instance | Construct a class from a class value and arg list |

## Conversion

| Function | Returns | Description |
|----------|---------|-------------|
| `Reflect.as_string(value)` | `String` | Convert value to string representation |
| `Reflect.as_list(value)` | `List` | Cast/convert to list |
| `Reflect.as_map(value)` | `Map` | Cast/convert to map |
| `Reflect.number_to_string(n)` | `String` | Convert number to string |

## Example

```saffron
import "@reflect" as Reflect

class Point {
    var x: Number
    var y: Number
    fun init(x: Number, y: Number) {
        this.x = x
        this.y = y
    }
}

var p = Point(1.0, 2.0)
IO.println(Reflect.type_of(p))       // "Point"
IO.println(Reflect.class_name(p))    // "Point"
IO.println(Reflect.fields(p))        // ["x", "y"]
IO.println(Reflect.is_instance(p))   // true
IO.println(Reflect.is_number(42))    // true
```

## Docstrings

Functions and classes documented with `///` comments expose their docs at runtime:

```saffron
/// Compute the square of a number.
fun square(x: Number): Number {
    return x * x
}

IO.println(Reflect.doc(square))  // "Compute the square of a number."
```
