# Reflect

```saffron
import "@reflect" as Reflect
```

Runtime type introspection for the native LLVM compiler.

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Reflect.type_name(value)` | `String` | Runtime type/class name of a value |
| `Reflect.is_instance(value)` | `Bool` | True if value is a class instance (not a primitive) |
| `Reflect.fields(instance)` | `Map<String, Any>` | Map of field names to current values |
| `Reflect.construct(class, data)` | instance | Construct a class instance from a class value and a field map |
| `Reflect.number_to_string(n)` | `String` | Convert a number to its string representation |

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
IO.println(Reflect.type_name(p))     // "Point"
IO.println(Reflect.is_instance(p))   // true
IO.println(Reflect.fields(p))        // {"x": 1.0, "y": 2.0}
IO.println(Reflect.number_to_string(42))  // "42"
```

## Notes

- `Reflect.fields()` returns a `Map<String, Any>` (not a list of names), where keys are field names and values are current field values
- The `is` operator can be used for type checks in user code: `42 is Number`, `"hi" is String`, `value is MyClass`
- These functions are only available in the LLVM-compiled native path, not the C VM interpreter
