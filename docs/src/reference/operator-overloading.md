# Operator Overloading

Classes can define methods that are invoked when operators are used on instances.

## Supported operators

| Method | Operator | Example |
|--------|----------|---------|
| `add(other)` | `+` | `a + b` |
| `sub(other)` | `-` | `a - b` |
| `mul(other)` | `*` | `a * b` |
| `div(other)` | `/` | `a / b` |
| `mod(other)` | `%` | `a % b` |
| `lt(other)` | `<` | `a < b` |
| `gt(other)` | `>` | `a > b` |
| `eq(other)` | `==` | `a == b` |

## Example

```saffron
class Vec3 {
    var x: Float
    var y: Float
    var z: Float

    fun init(x: Float, y: Float, z: Float) {
        this.x = x
        this.y = y
        this.z = z
    }

    fun add(other: Vec3): Vec3 {
        return Vec3(this.x + other.x, this.y + other.y, this.z + other.z)
    }

    fun sub(other: Vec3): Vec3 {
        return Vec3(this.x - other.x, this.y - other.y, this.z - other.z)
    }

    fun mul(scalar: Float): Vec3 {
        return Vec3(this.x * scalar, this.y * scalar, this.z * scalar)
    }

    fun eq(other: Vec3): Bool {
        return this.x == other.x and this.y == other.y and this.z == other.z
    }

    fun lt(other: Vec3): Bool {
        return this.length() < other.length()
    }

    fun length(): Float {
        return (this.x * this.x + this.y * this.y + this.z * this.z).sqrt()
    }
}

var a = Vec3(1.0, 2.0, 3.0)
var b = Vec3(4.0, 5.0, 6.0)

var c = a + b          // Vec3(5.0, 7.0, 9.0)
var d = a * 2.0        // Vec3(2.0, 4.0, 6.0)
IO.println(a == a)     // true
IO.println(a < b)      // true
```

## Notes

- Operator methods must accept exactly one parameter (the right-hand operand)
- The return type is up to you — `add` doesn't have to return the same type
- `!=` is automatically derived from `eq` (it's the negation)
- `<=` and `>=` are derived from `lt`, `gt`, and `eq`
