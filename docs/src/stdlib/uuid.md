# UUID

```saffron
import "@uuid" as UUID
```

Generate and parse UUIDs (RFC 4122).

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `UUID.v4()` | `String` | Generate a random UUID v4 |
| `UUID.is_valid(s)` | `Bool` | Check if a string is a valid UUID |
| `UUID.parse(s)` | `Map<String, String>` | Parse UUID into components |
| `UUID.nil_uuid()` | `String` | The nil UUID (all zeros) |
| `UUID.eq(a, b)` | `Bool` | Compare two UUIDs (case-insensitive) |
| `UUID.version(s)` | `Number` | Extract the version number from a UUID |

## Example

```saffron
import "@uuid" as UUID

// Generate a random UUID
var id = UUID.v4()
IO.println(id)  // "550e8400-e29b-41d4-a716-446655440000" (random)

// Validate
IO.println(UUID.is_valid(id))                    // true
IO.println(UUID.is_valid("not-a-uuid"))          // false

// Parse into components
var parts = UUID.parse(id)
IO.println(parts.get("version"))  // "4"

// Compare (case-insensitive)
IO.println(UUID.eq("ABC-...", "abc-..."))  // true

// Nil UUID
IO.println(UUID.nil_uuid())  // "00000000-0000-0000-0000-000000000000"

// Version
IO.println(UUID.version(id))  // 4
```
