# Bitwise Operators

Saffron supports bitwise operations on integers. These operators work on the underlying 64-bit integer representation.

## Operators

| Operator | Name | Example | Result |
|----------|------|---------|--------|
| `&` | Bitwise AND | `0xF0 & 0x0F` | `0` |
| `\|` | Bitwise OR | `0xF0 \| 0x0F` | `0xFF` |
| `^` | Bitwise XOR | `0xFF ^ 0x0F` | `0xF0` |
| `~` | Bitwise NOT | `~0` | `-1` |
| `<<` | Left shift | `1 << 4` | `16` |
| `>>` | Right shift (arithmetic) | `16 >> 2` | `4` |

## Precedence

From lowest to highest:

1. `or` (logical)
2. `and` (logical)
3. `==`, `!=`, `<`, `<=`, `>`, `>=` (comparison)
4. `|` (bitwise OR)
5. `^` (bitwise XOR)
6. `&` (bitwise AND)
7. `<<`, `>>` (shifts)
8. `+`, `-` (additive)
9. `*`, `/`, `%` (multiplicative)
10. `~`, `-`, `!` (unary)

Use parentheses when mixing bitwise and arithmetic operators to make intent clear.

## Hex Literals

Saffron supports hexadecimal integer literals with the `0x` or `0X` prefix:

```saffron
var mask = 0xFF
var flags = 0xDEADBEEF
var high_byte = 0xFF000000
```

## Examples

### Bit masking

```saffron
var color = 0xFF8040
var red   = (color >> 16) & 0xFF   // 255
var green = (color >> 8) & 0xFF    // 128
var blue  = color & 0xFF           // 64
```

### Setting and clearing bits

```saffron
var flags = 0

// Set bit 3
flags = flags | (1 << 3)   // 8

// Check if bit 3 is set
var is_set = (flags & (1 << 3)) != 0   // true

// Clear bit 3
flags = flags & ~(1 << 3)  // 0
```

### Flag enums with bitwise operations

```saffron
// Define permission flags as powers of 2
var READ    = 1 << 0   // 1
var WRITE   = 1 << 1   // 2
var EXECUTE = 1 << 2   // 4

// Combine permissions
var user_perms = READ | WRITE | EXECUTE   // 7
var guest_perms = READ                    // 1

// Check permission
fun has_permission(perms: Int, flag: Int): Bool {
    return (perms & flag) != 0
}

IO.println(has_permission(user_perms, WRITE))    // true
IO.println(has_permission(guest_perms, WRITE))   // false

// Remove a permission
var restricted = user_perms & ~EXECUTE   // 3 (READ | WRITE)
```

### XOR tricks

```saffron
// Swap without temporary
var a = 42
var b = 99
a = a ^ b
b = a ^ b
a = a ^ b
// a == 99, b == 42

// Toggle bits
var mask = 0x0F
var value = 0xAA
var toggled = value ^ mask   // flips lower 4 bits
```

## Notes

- All bitwise operators work on `Int` values (64-bit signed integers).
- `>>` is an arithmetic right shift (preserves the sign bit). Negative numbers shifted right remain negative.
- Shift amounts are not bounds-checked; shifting by 64 or more is undefined behavior.
- `~` is a unary prefix operator with the same precedence as `-` (negation) and `!` (logical not).
