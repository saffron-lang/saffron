# Binary Data Manipulation for Saffron

## Status

- **Stage:** Design / Proposal
- **Author:** --
- **Date:** 2026-06-02
- **Depends on:** Bitwise operators (partially implemented), GC heap object system
- **Depended on by:** `@encoding` module (unicode-support.md), `@crypto` (future), `@net` rewrite

---

## 1. Motivation

Saffron currently has no first-class support for binary data. The only way to work with
raw bytes is through unsafe `@extern` C functions and manual pointer arithmetic, as seen
in `src/lib/socket.sf`:

```saffron
@extern("void* malloc(i64)") fun sock_malloc(size: Int): Int
// ...
var buf: Int = sock_malloc(max_bytes + 1)
var n: Int = tcp_read_raw(fd, buf + total, max_bytes - total)
@intrinsic fun store8(addr: Int, val: Int)
store8(buf + total, 0)
```

This is error-prone, unsafe, and inaccessible to user code. Binary data support is
essential for:

- **File formats:** Reading/writing PNG, WASM, ZIP, PDF, protobuf, MessagePack
- **Network protocols:** HTTP/2 framing, WebSocket binary messages, DNS packets, TLS
- **FFI interop:** Passing structured data to C libraries (OpenSSL, SQLite, libpng)
- **Encoding/decoding:** The `@encoding` module (see unicode-support.md) needs to
  manipulate raw bytes to convert between UTF-8 and other character encodings
- **Cryptography:** Hash functions, HMAC, AES all operate on byte sequences
- **Performance:** Compact representation for large datasets (images, audio, tensors)
- **WASM:** Linear memory access in future WASM interop

Every mature language provides this:
- Rust: `&[u8]`, `Vec<u8>`, `bytes` crate
- Go: `[]byte`, `encoding/binary`, `bytes.Buffer`
- Python: `bytes`, `bytearray`, `struct`, `io.BytesIO`
- Node.js: `Buffer`, `TypedArray`, `DataView`
- Zig: `[]u8` with comptime generics

Saffron needs a safe, ergonomic, GC-managed byte sequence type with structured
read/write capabilities.

---

## 2. Bitwise Operators (Language-Level)

### 2.1 Current Status

Bitwise operators are **already partially implemented** in the Saffron compiler:

- **Lexer** (`src/compiler/lexer.sf`): Tokens `TkAmpersand`, `TkPipe`, `TkCaret`,
  `TkTilde`, `TkShiftLeft`, `TkShiftRight` are defined and scanned.
- **Parser** (`src/compiler/parser.sf`): Precedence levels exist with
  `parse_bitwise_or`, `parse_bitwise_xor`, `parse_bitwise_and` functions.
- **Codegen** (`src/compiler/codegen/expr_body.sf`): The `gen_binary` function already
  emits correct LLVM IR:

```saffron
// In gen_binary, after arithmetic operators:
else if (op == "&") { this.emit_indent(local + " = and i64 " + lhs + ", " + rhs) }
else if (op == "|") { this.emit_indent(local + " = or i64 " + lhs + ", " + rhs) }
else if (op == "^") { this.emit_indent(local + " = xor i64 " + lhs + ", " + rhs) }
else if (op == "<<") { this.emit_indent(local + " = shl i64 " + lhs + ", " + rhs) }
else if (op == ">>") { this.emit_indent(local + " = ashr i64 " + lhs + ", " + rhs) }
```

The unary `~` (bitwise NOT) is also implemented in `gen_unary`:

```saffron
if (op == "~") {
    var raw: String = this.emit_untag_int(rhs)
    var flipped: String = this.fresh_local()
    this.emit_indent(flipped + " = xor i64 " + raw + ", -1")
    var local: String = this.emit_tag_int(flipped)
    this.last_type = AST.Type.IntType
    return local
}
```

### 2.2 Operator Summary

| Operator | Name | LLVM Instruction | Example |
|----------|------|------------------|---------|
| `&` | Bitwise AND | `and i64` | `0xFF & 0x0F` -> `0x0F` |
| `\|` | Bitwise OR | `or i64` | `0x0F \| 0xF0` -> `0xFF` |
| `^` | Bitwise XOR | `xor i64` | `0xFF ^ 0xAA` -> `0x55` |
| `~` | Bitwise NOT | `xor i64 x, -1` | `~0xFF` -> all-bits-except-low-8 |
| `<<` | Left shift | `shl i64` | `1 << 4` -> `16` |
| `>>` | Arithmetic right shift | `ashr i64` | `-8 >> 1` -> `-4` |

### 2.3 Unsigned Right Shift (New)

The current `>>` performs arithmetic right shift (sign-extending). For binary data
manipulation, we also need logical (unsigned) right shift that zero-fills:

```saffron
var x: Int = -1                    // all bits set (0xFFFFFFFFFFFFFFFF)
var arith: Int = x >> 1            // still -1 (sign-extended) — 0xFFFFFFFFFFFFFFFF
var logic: Int = x >>> 1           // 0x7FFFFFFFFFFFFFFF (zero-filled from left)
```

**Implementation:**

- **Lexer:** Add `TkShiftRightUnsigned` token for `>>>`
- **Parser:** Handle in `parse_shift` at the same precedence as `>>` and `<<`
- **Codegen:** Emit `lshr i64` instead of `ashr i64`

### 2.4 Operator Precedence

The parser already implements the correct C-family precedence:

```
(lowest)
    ||              logical or
    &&              logical and
    |               bitwise or
    ^               bitwise xor
    &               bitwise and
    == !=           equality
    < <= > >=       comparison
    << >> >>>       shift
    + -             additive
    * / %           multiplicative
    ~ ! -           unary prefix
(highest)
```

### 2.5 Numeric Literals

**Hex literals** (`0x` / `0X`) are already supported in the lexer:

```saffron
var mask: Int = 0xFF
var rgb: Int = 0xFF8800
```

**Binary literals** (`0b` / `0B`) and **octal literals** (`0o` / `0O`) are NOT yet
supported and should be added:

```saffron
var flags: Int = 0b11001010          // binary: 202
var perms: Int = 0o755               // octal: 493
```

**Implementation in `read_number()`:**

```saffron
// After the existing hex check (0x/0X), add:
if (next_ch == "b" or next_ch == "B") {
    this.advance() // consume 'b'
    while (this.pos < this.source.length() and this.is_binary_digit(this.peek())) {
        this.advance()
    }
    var text: String = this.source.slice(start + 2, this.pos)
    var value: Float = this.parse_binary(text)
    this.emit(TokenKind.TkInt(value))
    return
}
if (next_ch == "o" or next_ch == "O") {
    this.advance() // consume 'o'
    while (this.pos < this.source.length() and this.is_octal_digit(this.peek())) {
        this.advance()
    }
    var text: String = this.source.slice(start + 2, this.pos)
    var value: Float = this.parse_octal(text)
    this.emit(TokenKind.TkInt(value))
    return
}
```

Helper functions needed:

```saffron
fun is_binary_digit(ch: String): Bool {
    return ch == "0" or ch == "1"
}

fun is_octal_digit(ch: String): Bool {
    return ch >= "0" and ch <= "7"
}

fun parse_binary(text: String): Float {
    var result: Float = 0
    var i: Float = 0
    while (i < text.length()) {
        result = result * 2
        if (text.char_at(i) == "1") { result = result + 1 }
        i = i + 1
    }
    return result
}

fun parse_octal(text: String): Float {
    var result: Float = 0
    var i: Float = 0
    while (i < text.length()) {
        result = result * 8 + (text.char_at(i).to_number())
        i = i + 1
    }
    return result
}
```

### 2.6 Numeric Literal Separators (Future)

For readability in long binary/hex constants, consider allowing `_` separators:

```saffron
var mask: Int = 0b1111_0000_1010_0101
var addr: Int = 0xFF_FF_00_00
var big: Int = 1_000_000
```

This is a nice-to-have and can be deferred to a later phase.

### 2.7 Operator Overloading for Bitwise Ops

The existing operator overloading system supports `add`, `sub`, `mul`, `div`, `mod`,
`lt`, `gt`, `eq`. Extend it to include bitwise operators:

| Operator | Method name |
|----------|-------------|
| `&` | `bit_and` |
| `\|` | `bit_or` |
| `^` | `bit_xor` |
| `<<` | `shl` |
| `>>` | `shr` |
| `>>>` | `ushr` |

This enables the Buffer type to support `buf1 & buf2` syntax (see Section 8).

---

## 3. Buffer Type

### 3.1 Design Principles

- **Not a string.** No null terminator, no UTF-8 assumption, no string interning.
- **Fixed-size after creation.** Buffers have a fixed length set at allocation time.
  Growable semantics are provided by `BufferWriter` (Section 7).
- **Mutable.** Individual bytes can be written without creating a new buffer.
- **GC-managed.** The garbage collector tracks buffer allocations and frees them when
  unreachable.
- **Bounds-checked.** Every access validates the index (with fast-path elision possible
  in release mode).
- **Value is an Int (0-255).** Bytes are represented as integers in the 0-255 range.

### 3.2 Creation

```saffron
import "@buffer" as Buffer

// Allocate N zero-filled bytes
var buf: Buffer = Buffer.alloc(1024)

// From a list of byte values (each must be 0-255)
var buf2: Buffer = Buffer.from_list([0x48, 0x65, 0x6C, 0x6C, 0x6F])

// From a string's UTF-8 bytes (copies the bytes, no shared ownership)
var buf3: Buffer = Buffer.from_string("hello")

// From hex string
var buf4: Buffer = Buffer.from_hex("48656c6c6f")

// From base64
var buf5: Buffer = Buffer.from_base64("aGVsbG8=")

// Wrap existing memory (unsafe, for FFI interop only)
// The caller is responsible for lifetime management
var buf6: Buffer = Buffer.wrap(ptr, length)
```

### 3.3 Properties and Basic Operations

```saffron
buf.length()           // byte count (always O(1))

// Indexed access — returns/accepts Int (0-255)
var byte: Int = buf[0]         // read byte at index
buf[0] = 0xFF                  // write byte at index
buf[-1] = 0x00                 // negative indexing (last byte)

// Slicing — returns a NEW buffer (copy semantics)
var sub: Buffer = buf.slice(4, 8)       // bytes [4, 5, 6, 7]
var tail: Buffer = buf.slice(4)         // bytes [4..end]
var head: Buffer = buf.slice(0, 4)      // bytes [0, 1, 2, 3]

// Equality (byte-by-byte comparison)
buf.equals(other)              // true if same length and same bytes

// Fill
buf.fill(0)                    // zero out entire buffer
buf.fill(0xFF, 4, 8)          // fill bytes [4..8) with 0xFF

// Copy between buffers
buf.copy_from(src, src_offset, dst_offset, length)

// Concatenation (returns new buffer)
var combined: Buffer = buf.concat(other)
var multi: Buffer = Buffer.concat_list([buf1, buf2, buf3])
```

### 3.4 Conversion Methods

```saffron
// To string (interprets bytes as UTF-8)
buf.to_string()                // throws on invalid UTF-8
buf.to_string_lossy()          // replaces invalid sequences with U+FFFD

// To/from collections
buf.to_list()                  // List<Int> of byte values [72, 101, 108, ...]
Buffer.from_list(list)         // List<Int> -> Buffer

// Hex encoding/decoding
buf.to_hex()                   // "48656c6c6f"
buf.to_hex_upper()             // "48656C6C6F"
Buffer.from_hex("48656c6c6f")  // decode hex -> Buffer (throws on invalid hex)

// Base64 encoding/decoding
buf.to_base64()                // "aGVsbG8="
buf.to_base64_url()            // URL-safe base64 (- and _ instead of + and /)
Buffer.from_base64("aGVsbG8=") // decode base64 -> Buffer
```

---

## 4. Typed Read/Write Methods on Buffer

For reading and writing multi-byte values at specific offsets. These are the building
blocks for protocol parsing and binary file format handling.

### 4.1 Unsigned Integer Reads

```saffron
buf.read_u8(offset)            // 1 byte  -> Int (0..255)
buf.read_u16_le(offset)        // 2 bytes -> Int (little-endian)
buf.read_u16_be(offset)        // 2 bytes -> Int (big-endian)
buf.read_u32_le(offset)        // 4 bytes -> Int (little-endian)
buf.read_u32_be(offset)        // 4 bytes -> Int (big-endian)
buf.read_u64_le(offset)        // 8 bytes -> Int (little-endian)
buf.read_u64_be(offset)        // 8 bytes -> Int (big-endian)
```

### 4.2 Signed Integer Reads

```saffron
buf.read_i8(offset)            // 1 byte  -> Int (sign-extended, -128..127)
buf.read_i16_le(offset)        // 2 bytes -> Int (little-endian, signed)
buf.read_i16_be(offset)        // 2 bytes -> Int (big-endian, signed)
buf.read_i32_le(offset)        // 4 bytes -> Int (little-endian, signed)
buf.read_i32_be(offset)        // 4 bytes -> Int (big-endian, signed)
buf.read_i64_le(offset)        // 8 bytes -> Int (little-endian, signed)
buf.read_i64_be(offset)        // 8 bytes -> Int (big-endian, signed)
```

### 4.3 Floating Point Reads

```saffron
buf.read_f32_le(offset)        // 4 bytes -> Float (IEEE 754, little-endian)
buf.read_f32_be(offset)        // 4 bytes -> Float (IEEE 754, big-endian)
buf.read_f64_le(offset)        // 8 bytes -> Float (IEEE 754, little-endian)
buf.read_f64_be(offset)        // 8 bytes -> Float (IEEE 754, big-endian)
```

### 4.4 Write Methods

Every read method has a corresponding write:

```saffron
buf.write_u8(offset, value)
buf.write_u16_le(offset, value)
buf.write_u16_be(offset, value)
buf.write_u32_le(offset, value)
buf.write_u32_be(offset, value)
buf.write_u64_le(offset, value)
buf.write_u64_be(offset, value)

buf.write_i8(offset, value)
buf.write_i16_le(offset, value)
buf.write_i16_be(offset, value)
buf.write_i32_le(offset, value)
buf.write_i32_be(offset, value)
buf.write_i64_le(offset, value)
buf.write_i64_be(offset, value)

buf.write_f32_le(offset, value)
buf.write_f32_be(offset, value)
buf.write_f64_le(offset, value)
buf.write_f64_be(offset, value)
```

### 4.5 Bounds Checking

All read/write methods validate that `offset + size <= buf.length()`. On violation:

```saffron
var small: Buffer = Buffer.alloc(4)
try {
    small.read_u32_le(2)   // needs bytes [2,3,4,5] but buffer is only [0,1,2,3]
} catch (e) {
    // "Buffer out of bounds: read_u32_le at offset 2 requires 4 bytes, buffer length is 4"
    IO.println(e)
}
```

### 4.6 Endianness Helper

For code that needs to work with the system's native endianness:

```saffron
Buffer.is_little_endian()      // true on x86/ARM-LE, false on big-endian systems

// Convenience: read/write in native byte order
buf.read_u32_ne(offset)        // "ne" = native endian
buf.write_u32_ne(offset, value)
```

### 4.7 Implementation Notes (LLVM)

Multi-byte reads compile to straightforward shift-and-OR sequences:

```llvm
; read_u16_le at offset %off from buffer data pointer %data
%p = getelementptr i8, ptr %data, i64 %off
%b0 = load i8, ptr %p                       ; low byte
%b1.ptr = getelementptr i8, ptr %p, i64 1
%b1 = load i8, ptr %b1.ptr                  ; high byte
%b0.ext = zext i8 %b0 to i64
%b1.ext = zext i8 %b1 to i64
%b1.shifted = shl i64 %b1.ext, 8
%result = or i64 %b0.ext, %b1.shifted
```

For big-endian reads, reverse the byte order. The LLVM `@llvm.bswap.i16/i32/i64`
intrinsics can be used when loading natively and swapping:

```llvm
; read_u32_be on a little-endian host: load native, then bswap
%raw = load i32, ptr %p
%swapped = call i32 @llvm.bswap.i32(i32 %raw)
%result = zext i32 %swapped to i64
```

---

## 5. @struct Module -- Pack/Unpack

### 5.1 Concept

The `@struct` module provides Python-style format strings for converting between
Saffron values and their binary representations. This is the ergonomic layer above
raw `read_*/write_*` calls.

### 5.2 Format String Syntax

```
Byte-order prefix (first character):
    <    little-endian
    >    big-endian
    !    network byte order (big-endian)
    =    native byte order

Format characters:
    b    i8   (signed byte)
    B    u8   (unsigned byte)
    h    i16  (signed short)
    H    u16  (unsigned short)
    i    i32  (signed int)
    I    u32  (unsigned int)
    q    i64  (signed long)
    Q    u64  (unsigned long)
    f    f32  (float)
    d    f64  (double)
    s    string (preceded by count: "4s" = 4-byte string)
    x    padding byte (no value consumed/produced)
    ?    bool (1 byte: 0 = false, non-zero = true)

Repeat counts:
    3I   three consecutive u32 values
    16s  16-byte fixed string
    4x   4 padding bytes
```

### 5.3 Basic Pack/Unpack

```saffron
import "@struct" as Struct

// Pack values into a Buffer
var buf: Buffer = Struct.pack("<IHB", [1024, 80, 6])
// Result: 7 bytes
//   [0x00, 0x04, 0x00, 0x00,   // u32_le(1024)
//    0x50, 0x00,                 // u16_le(80)
//    0x06]                       // u8(6)

// Unpack a Buffer into a list of values
var values: List<Any> = Struct.unpack("<IHB", buf)
// [1024, 80, 6]

// Calculate the packed size of a format string
var size: Int = Struct.size("<IHB")   // 7
var size2: Int = Struct.size(">3Id")  // 3*4 + 8 = 20
```

### 5.4 Named Struct Layouts

For repeated use (e.g., parsing many records of the same format), define a layout once:

```saffron
import "@struct" as Struct

// Define a named layout
var ip_header: Struct.Layout = Struct.layout({
    "version_ihl": "B",
    "dscp_ecn":    "B",
    "total_len":   "H",
    "ident":       "H",
    "flags_frag":  "H",
    "ttl":         "B",
    "protocol":    "B",
    "checksum":    "H",
    "src_addr":    "I",
    "dst_addr":    "I"
}, ">")  // network byte order

// Pack from a map
var pkt: Buffer = ip_header.pack({
    "version_ihl": 0x45,
    "dscp_ecn":    0,
    "total_len":   40,
    "ident":       0x1234,
    "flags_frag":  0x4000,
    "ttl":         64,
    "protocol":    6,
    "checksum":    0,
    "src_addr":    0xC0A80001,
    "dst_addr":    0xC0A80002
})

// Unpack to a map
var header: Map<String, Any> = ip_header.unpack(pkt)
IO.println(header.get("protocol"))  // 6
IO.println(header.get("ttl"))       // 64

// Get the total byte size
ip_header.size()   // 20
```

### 5.5 Nested Layouts and Arrays

```saffron
// Fixed-size arrays within a struct
var pixel_format: Struct.Layout = Struct.layout({
    "r": "B",
    "g": "B",
    "b": "B",
    "a": "B"
}, "<")

// Parse N pixels from a buffer
var pixels: List<Map<String, Any>> = Struct.unpack_array(pixel_format, image_data, num_pixels)
```

### 5.6 Alignment and Padding

Struct layouts do NOT automatically add padding (unlike C structs). Use explicit `x`
padding bytes when alignment matters:

```saffron
// C struct with natural alignment:
// struct { uint8_t type; uint32_t value; }
// has 3 bytes padding between type and value

var aligned: Struct.Layout = Struct.layout({
    "type":    "B",
    "padding": "3x",    // explicit 3 padding bytes
    "value":   "I"
}, "<")
// Total size: 8 bytes
```

### 5.7 String Fields

Fixed-length string fields are padded with nulls on pack and trimmed on unpack:

```saffron
var record: Struct.Layout = Struct.layout({
    "magic":   "4s",    // exactly 4 bytes
    "name":    "32s",   // exactly 32 bytes (null-padded)
    "version": "H"
}, "<")

var buf: Buffer = record.pack({
    "magic":   "SAFF",
    "name":    "my_module",    // padded to 32 bytes with \0
    "version": 1
})

var data: Map<String, Any> = record.unpack(buf)
data.get("name")    // "my_module" (trailing nulls stripped)
```

---

## 6. Binary File I/O

### 6.1 Read/Write Entire Files

```saffron
import "@buffer" as Buffer

// Read entire file as raw bytes (no encoding assumed)
var data: Buffer = Buffer.read_file("image.png")

// Write raw bytes to file (creates or truncates)
Buffer.write_file("output.bin", data)

// Append bytes to existing file
Buffer.append_file("log.bin", extra_bytes)
```

### 6.2 Comparison with Text I/O

```saffron
// Text I/O (existing) — assumes UTF-8, returns String
var text: String = IO.read_file("readme.txt")

// Binary I/O (new) — no encoding, returns Buffer
var data: Buffer = Buffer.read_file("image.png")
```

### 6.3 Streaming File I/O (Future)

For large files where loading everything into memory is impractical:

```saffron
import "@buffer" as Buffer

// Read in chunks
var file: Buffer.FileReader = Buffer.open_read("huge.bin")
while (!file.eof()) {
    var chunk: Buffer = file.read(4096)   // read up to 4096 bytes
    process(chunk)
}
file.close()

// Write in chunks
var out: Buffer.FileWriter = Buffer.open_write("output.bin")
out.write(header_buf)
out.write(payload_buf)
out.close()
```

This is deferred to a later phase. Initial implementation focuses on whole-file
read/write which covers most use cases (file formats, configs, small binary data).

### 6.4 Runtime Implementation

For the LLVM path, file I/O compiles to calls to C runtime functions:

```c
// runtime/buffer_io.c
int64_t __buffer_read_file(int64_t path_str);       // -> Buffer value
void    __buffer_write_file(int64_t path_str, int64_t buf_val);
void    __buffer_append_file(int64_t path_str, int64_t buf_val);
```

These use standard POSIX `open`/`read`/`write`/`close` (or `fopen`/`fread`/`fwrite`
on Windows).

---

## 7. BufferReader / BufferWriter (Cursor-Based)

### 7.1 Motivation

Manual offset tracking is tedious and error-prone when parsing sequential binary data:

```saffron
// Without cursors (manual offset tracking):
var offset: Int = 0
var magic: Int = buf.read_u32_be(offset)
offset = offset + 4
var version: Int = buf.read_u16_le(offset)
offset = offset + 2
var count: Int = buf.read_u32_le(offset)
offset = offset + 4
// Easy to get offsets wrong, especially with variable-length fields
```

Cursor-based readers/writers maintain position automatically.

### 7.2 BufferReader

```saffron
import "@buffer" as Buffer

var reader: Buffer.Reader = Buffer.reader(data)

// Sequential reads (cursor advances automatically)
var magic: String = reader.read_string(4)       // read 4 bytes as UTF-8 string
var version: Int = reader.read_u16_le()         // advances cursor by 2
var flags: Int = reader.read_u32_be()           // advances cursor by 4
var name_len: Int = reader.read_u8()            // advances cursor by 1
var name: String = reader.read_string(name_len) // variable-length read

// Skip bytes (padding, reserved fields)
reader.skip(8)

// Read raw bytes as a sub-buffer
var payload: Buffer = reader.read_bytes(count)

// Position management
reader.position()              // current byte offset
reader.remaining()             // bytes left until end
reader.seek(offset)            // jump to absolute offset
reader.rewind()                // reset to beginning

// Peek without advancing
var next_byte: Int = reader.peek_u8()
var next_u32: Int = reader.peek_u32_le()

// Check for end
reader.eof()                   // true if no bytes remaining

// All typed reads available:
reader.read_u8()
reader.read_u16_le()
reader.read_u16_be()
reader.read_u32_le()
reader.read_u32_be()
reader.read_u64_le()
reader.read_u64_be()
reader.read_i8()
reader.read_i16_le()
reader.read_i16_be()
reader.read_i32_le()
reader.read_i32_be()
reader.read_i64_le()
reader.read_i64_be()
reader.read_f32_le()
reader.read_f32_be()
reader.read_f64_le()
reader.read_f64_be()
```

### 7.3 BufferWriter

```saffron
import "@buffer" as Buffer

// Create with initial capacity (grows automatically)
var writer: Buffer.Writer = Buffer.writer(1024)

// Sequential writes (cursor advances, buffer grows if needed)
writer.write_string("SAFF")            // write UTF-8 bytes of string
writer.write_u16_le(1)                 // write 2 bytes
writer.write_u32_be(payload.length())  // write 4 bytes
writer.write_bytes(payload)            // write raw buffer contents
writer.write_u8(0)                     // write single byte

// Padding
writer.write_zeros(16)                 // write 16 zero bytes
writer.align(4)                        // pad to next 4-byte boundary

// All typed writes available:
writer.write_u8(value)
writer.write_u16_le(value)
writer.write_u16_be(value)
writer.write_u32_le(value)
writer.write_u32_be(value)
writer.write_u64_le(value)
writer.write_u64_be(value)
writer.write_i8(value)
writer.write_i16_le(value)
writer.write_i16_be(value)
writer.write_i32_le(value)
writer.write_i32_be(value)
writer.write_i64_le(value)
writer.write_i64_be(value)
writer.write_f32_le(value)
writer.write_f32_be(value)
writer.write_f64_le(value)
writer.write_f64_be(value)

// Finalize: get the written bytes as a fixed-size Buffer
var result: Buffer = writer.to_buffer()

// Query
writer.position()                      // bytes written so far
writer.capacity()                      // current allocation size
```

### 7.4 Example: PNG Chunk Writer

```saffron
import "@buffer" as Buffer
import "@struct" as Struct

fun write_png_chunk(chunk_type: String, data: Buffer): Buffer {
    var writer: Buffer.Writer = Buffer.writer(data.length() + 12)

    // Length (4 bytes, big-endian)
    writer.write_u32_be(data.length())

    // Type (4 ASCII bytes)
    writer.write_string(chunk_type)

    // Data
    writer.write_bytes(data)

    // CRC32 over type + data (placeholder)
    var crc_data: Buffer = Buffer.concat_list([
        Buffer.from_string(chunk_type),
        data
    ])
    writer.write_u32_be(compute_crc32(crc_data))

    return writer.to_buffer()
}
```

### 7.5 Example: Protocol Parser

```saffron
import "@buffer" as Buffer

fun parse_dns_header(data: Buffer): Map<String, Any> {
    var reader: Buffer.Reader = Buffer.reader(data)

    var result: Map<String, Any> = {}
    result.set("id", reader.read_u16_be())
    var flags: Int = reader.read_u16_be()
    result.set("qr", (flags >> 15) & 1)
    result.set("opcode", (flags >> 11) & 0xF)
    result.set("aa", (flags >> 10) & 1)
    result.set("tc", (flags >> 9) & 1)
    result.set("rd", (flags >> 8) & 1)
    result.set("ra", (flags >> 7) & 1)
    result.set("rcode", flags & 0xF)
    result.set("qdcount", reader.read_u16_be())
    result.set("ancount", reader.read_u16_be())
    result.set("nscount", reader.read_u16_be())
    result.set("arcount", reader.read_u16_be())

    return result
}
```

---

## 8. Bitwise Operations on Buffer

Bulk bitwise operations over entire buffers are useful for XOR-based encryption,
masking, checksums, and bitmap manipulation.

### 8.1 Buffer-Level Bitwise Methods

```saffron
// XOR two buffers of equal length (returns new Buffer)
var encrypted: Buffer = plaintext.xor(key_stream)

// AND with a mask buffer
var masked: Buffer = data.and_buf(mask)

// OR two buffers
var combined: Buffer = layer1.or_buf(layer2)

// Bitwise NOT every byte (returns new Buffer)
var inverted: Buffer = data.not_buf()

// Left/right shift all bytes (with carry between bytes — like a big integer)
var shifted: Buffer = data.shift_left(3)    // shift entire buffer left by 3 bits
var shifted2: Buffer = data.shift_right(1)  // shift right by 1 bit
```

### 8.2 Length Mismatch Behavior

Operations on buffers of different lengths throw an error:

```saffron
var a: Buffer = Buffer.alloc(4)
var b: Buffer = Buffer.alloc(8)
try {
    var c: Buffer = a.xor(b)
} catch (e) {
    // "Buffer length mismatch: xor requires equal lengths (4 vs 8)"
}
```

### 8.3 XOR Encryption Example

```saffron
import "@buffer" as Buffer

fun xor_encrypt(data: Buffer, key: Buffer): Buffer {
    // Repeat key to match data length
    var key_stream: Buffer = Buffer.alloc(data.length())
    var i: Int = 0
    while (i < data.length()) {
        key_stream[i] = key[i % key.length()]
        i = i + 1
    }
    return data.xor(key_stream)
}

var message: Buffer = Buffer.from_string("secret message")
var key: Buffer = Buffer.from_string("key")
var encrypted: Buffer = xor_encrypt(message, key)
var decrypted: Buffer = xor_encrypt(encrypted, key)
IO.println(decrypted.to_string())  // "secret message"
```

### 8.4 Operator Overloading (Optional Sugar)

If operator overloading for bitwise ops is implemented (Section 2.7), these become:

```saffron
var result: Buffer = buf1 ^ buf2    // calls buf1.bit_xor(buf2)
var masked: Buffer = buf & mask     // calls buf.bit_and(mask)
```

---

## 9. Runtime Implementation

### 9.1 LLVM-Compiled Path: Object Layout

Buffer is a GC-managed heap object with this layout:

```
%Buffer = type {
    i64,     ; tag/header (GC metadata: object type tag, mark bit)
    i64,     ; length (byte count)
    i64,     ; capacity (allocated size, >= length)
    ptr      ; data (pointer to byte array)
}
```

Alternative (inline data for small buffers):

```
; For buffers <= 48 bytes, data is stored inline after the header
%BufferSmall = type {
    i64,         ; tag/header
    i64,         ; length
    [48 x i8]   ; inline data
}

; For larger buffers, data is a separate allocation
%BufferLarge = type {
    i64,         ; tag/header
    i64,         ; length
    i64,         ; capacity
    ptr          ; data (heap-allocated)
}
```

For Phase 1, use the simpler external-data layout. The small-buffer optimization can
be added later as a performance enhancement.

### 9.2 NaN-Boxing Representation

Since Saffron uses NaN-boxing for values (64-bit tagged values), a Buffer value is
represented as a tagged pointer:

```
Buffer value = pointer to %Buffer struct | BUFFER_TAG_BITS
```

The tag bits identify it as a Buffer (distinct from String, List, Map, etc.).
The runtime needs a new tag added to the existing `__val_is_*` family:

```c
// runtime/value.h
#define TAG_BUFFER  0x...  // new tag bits for Buffer type

// runtime/value.c
bool __val_is_buffer(int64_t val) {
    return (val & TAG_MASK) == TAG_BUFFER;
}
```

### 9.3 Runtime C Functions

Core allocation and access functions:

```c
// runtime/buffer.c

// Allocate a new zero-filled buffer of given size
int64_t __buffer_alloc(int64_t size);

// Create buffer from byte values in a list
int64_t __buffer_from_list(int64_t list_val);

// Get buffer length
int64_t __buffer_length(int64_t buf_val);

// Read/write single byte (bounds-checked)
int64_t __buffer_get_byte(int64_t buf_val, int64_t index);
void    __buffer_set_byte(int64_t buf_val, int64_t index, int64_t value);

// Multi-byte reads (each returns the value, throws on OOB)
int64_t __buffer_read_u16_le(int64_t buf_val, int64_t offset);
int64_t __buffer_read_u16_be(int64_t buf_val, int64_t offset);
int64_t __buffer_read_u32_le(int64_t buf_val, int64_t offset);
int64_t __buffer_read_u32_be(int64_t buf_val, int64_t offset);
int64_t __buffer_read_u64_le(int64_t buf_val, int64_t offset);
int64_t __buffer_read_u64_be(int64_t buf_val, int64_t offset);
int64_t __buffer_read_f32_le(int64_t buf_val, int64_t offset);
int64_t __buffer_read_f64_le(int64_t buf_val, int64_t offset);
// ... (signed variants sign-extend)

// Multi-byte writes
void __buffer_write_u16_le(int64_t buf_val, int64_t offset, int64_t value);
void __buffer_write_u32_be(int64_t buf_val, int64_t offset, int64_t value);
// ... etc

// Bulk operations
void    __buffer_fill(int64_t buf_val, int64_t byte_value, int64_t start, int64_t end);
void    __buffer_copy(int64_t dst, int64_t dst_off, int64_t src, int64_t src_off, int64_t len);
int64_t __buffer_slice(int64_t buf_val, int64_t start, int64_t end);
int64_t __buffer_equals(int64_t a, int64_t b);
int64_t __buffer_concat(int64_t a, int64_t b);

// Conversion
int64_t __buffer_to_string(int64_t buf_val);          // -> String (throws on invalid UTF-8)
int64_t __buffer_to_string_lossy(int64_t buf_val);    // -> String (lossy)
int64_t __buffer_from_string(int64_t str_val);        // -> Buffer
int64_t __buffer_to_hex(int64_t buf_val);             // -> String
int64_t __buffer_from_hex(int64_t str_val);           // -> Buffer
int64_t __buffer_to_base64(int64_t buf_val);          // -> String
int64_t __buffer_from_base64(int64_t str_val);        // -> Buffer

// Bitwise bulk ops
int64_t __buffer_xor(int64_t a, int64_t b);
int64_t __buffer_and(int64_t a, int64_t b);
int64_t __buffer_or(int64_t a, int64_t b);
int64_t __buffer_not(int64_t buf_val);

// File I/O
int64_t __buffer_read_file(int64_t path_str);
void    __buffer_write_file(int64_t path_str, int64_t buf_val);
void    __buffer_append_file(int64_t path_str, int64_t buf_val);
```

### 9.4 GC Integration

The garbage collector must know about Buffer objects:

1. **Registration:** When a Buffer is allocated, register it with the GC shadow stack
   (same pattern as strings, lists, maps).
2. **Memory tracking:** The GC must account for the buffer's data allocation in its
   memory pressure calculation (to trigger collection when too much buffer memory is
   allocated).
3. **Finalization:** When a Buffer is collected, free its data pointer (if externally
   allocated).
4. **Tracing:** Buffers contain no Saffron object references internally, so the GC
   mark phase does not need to trace into buffer data. The buffer header itself is the
   only GC-managed allocation.

```c
// In gc.c / memory.c:
void __gc_register_buffer(Buffer *buf) {
    __gc_track_external_memory(buf->capacity);  // memory pressure
    // Buffer is reachable from the shadow stack via the tagged i64 value
}

void __gc_finalize_buffer(Buffer *buf) {
    __gc_untrack_external_memory(buf->capacity);
    free(buf->data);
    free(buf);
}
```

### 9.5 CVM Implementation

For the C VM (bytecode interpreter), add a new heap object type:

```c
// cvm/object.h
typedef struct {
    Obj obj;           // GC header
    int length;        // byte count
    int capacity;      // allocated size
    uint8_t *data;     // byte array
} ObjBuffer;

// Constructor
ObjBuffer* newBuffer(int size);
ObjBuffer* bufferFromBytes(const uint8_t *data, int length);
```

Register native methods via the existing method dispatch table (same pattern as
`ObjString` methods in `cvm/libc/string.c`).

---

## 10. Type System Integration

### 10.1 Buffer as a Built-in Type

`Buffer` is a new primitive type in the type system, alongside `Int`, `Float`,
`String`, `Bool`, `Nil`, `List<T>`, `Map<K,V>`:

```saffron
// Type annotations
var buf: Buffer = Buffer.alloc(256)
fun process(data: Buffer): Buffer { ... }

// Type checking
buf is Buffer              // true
"hello" is Buffer          // false

// In generic contexts
var cache: Map<String, Buffer> = {}
var buffers: List<Buffer> = []
```

### 10.2 Type Checker Changes

In `cvm/types.c` and the self-hosted type checker:
- Add `BufferType` to the type enum
- Buffer methods resolve through the method dispatch table
- Index operations (`buf[i]`) return `Int` and accept `Int` assignment
- No implicit conversion between String and Buffer

### 10.3 Codegen Changes

In `src/compiler/codegen/methods_body.sf`:
- Add a `Buffer` section to method dispatch (similar to existing String/List/Map sections)
- Map method names to `__buffer_*` runtime calls
- Index get/set on Buffer values routes to `__buffer_get_byte`/`__buffer_set_byte`

---

## 11. Interaction with Other Modules

### 11.1 @encoding (Unicode Support)

The `@encoding` module (defined in `unicode-support.md`) converts between byte sequences
and strings. With Buffers, its API becomes cleaner:

```saffron
import "@encoding" as Encoding
import "@buffer" as Buffer

// Decode bytes in a specific encoding to a UTF-8 string
var raw: Buffer = Buffer.read_file("legacy.csv")
var text: String = Encoding.decode(raw, "windows-1252")

// Encode a UTF-8 string to bytes in a target encoding
var encoded: Buffer = Encoding.encode(text, "shift_jis")
Buffer.write_file("output.csv", encoded)
```

This replaces the `List<Int>` approach in the unicode-support.md design with the
more appropriate `Buffer` type.

### 11.2 @socket (Network)

The socket module (`src/lib/socket.sf`) currently uses raw pointer arithmetic. With
Buffer, it becomes safe:

```saffron
import "@socket" as Socket
import "@buffer" as Buffer

var conn: Socket.Connection = Socket.connect("example.com", 80)

// Send binary data
var request: Buffer = Buffer.from_string("GET / HTTP/1.0\r\n\r\n")
conn.write(request)

// Receive binary data
var response: Buffer = conn.read(4096)
IO.println(response.to_string())
```

### 11.3 @crypto (Future)

```saffron
import "@crypto" as Crypto
import "@buffer" as Buffer

var data: Buffer = Buffer.from_string("hello world")
var hash: Buffer = Crypto.sha256(data)
IO.println(hash.to_hex())  // "b94d27b9934d3e08..."

var key: Buffer = Crypto.random_bytes(32)
var encrypted: Buffer = Crypto.aes_encrypt(data, key)
```

### 11.4 @wasm (Future)

```saffron
import "@wasm" as Wasm
import "@buffer" as Buffer

var module: Wasm.Module = Wasm.compile(Buffer.read_file("app.wasm"))
var instance: Wasm.Instance = module.instantiate({})

// Access linear memory as a Buffer
var memory: Buffer = instance.memory()
var result: Int = memory.read_u32_le(0x1000)
```

### 11.5 Turmeric Web Framework

For the WASM target, binary data enables efficient DOM serialization and
SharedArrayBuffer communication:

```saffron
import "@buffer" as Buffer

// Efficient binary protocol for host<->wasm communication
fun serialize_vdom_patch(patch: VDomPatch): Buffer {
    var writer: Buffer.Writer = Buffer.writer(64)
    writer.write_u8(patch.op_code())
    writer.write_u32_le(patch.node_id())
    writer.write_string(patch.attribute())
    return writer.to_buffer()
}
```

---

## 12. Performance Considerations

### 12.1 O(1) Byte Access

Buffer indexing must be O(1) -- direct pointer arithmetic with a bounds check:

```c
int64_t __buffer_get_byte(int64_t buf_val, int64_t index) {
    Buffer *buf = val_to_buffer(buf_val);
    int64_t idx = untag_int(index);
    if (idx < 0) idx += buf->length;  // negative indexing
    if (idx < 0 || idx >= buf->length) {
        __throw("Buffer index out of bounds");
    }
    return tag_int(buf->data[idx]);
}
```

### 12.2 Bulk Operations

`fill`, `copy_from`, `equals`, `xor`, `and_buf`, `or_buf`, `not_buf` should use
optimized C library functions:

```c
void __buffer_fill(Buffer *buf, uint8_t value, int64_t start, int64_t end) {
    memset(buf->data + start, value, end - start);
}

void __buffer_copy(Buffer *dst, int64_t dst_off, Buffer *src, int64_t src_off, int64_t len) {
    memmove(dst->data + dst_off, src->data + src_off, len);  // memmove handles overlap
}

int64_t __buffer_equals(Buffer *a, Buffer *b) {
    if (a->length != b->length) return 0;
    return memcmp(a->data, b->data, a->length) == 0;
}
```

For XOR/AND/OR bulk operations, process 8 bytes at a time using `uint64_t*` casts:

```c
int64_t __buffer_xor(Buffer *a, Buffer *b) {
    Buffer *result = alloc_buffer(a->length);
    int64_t i = 0;
    // Fast path: 8 bytes at a time
    int64_t fast_end = a->length & ~7;
    for (; i < fast_end; i += 8) {
        *(uint64_t*)(result->data + i) =
            *(uint64_t*)(a->data + i) ^ *(uint64_t*)(b->data + i);
    }
    // Remainder
    for (; i < a->length; i++) {
        result->data[i] = a->data[i] ^ b->data[i];
    }
    return tag_buffer(result);
}
```

### 12.3 SIMD (Future Optimization)

For very large buffers (>= 256 bytes), LLVM can auto-vectorize the bulk loops, or we
can use explicit SIMD intrinsics:

```llvm
; LLVM auto-vectorization hint
define void @buffer_xor_loop(ptr %dst, ptr %a, ptr %b, i64 %len) {
entry:
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %loop]
  %ap = getelementptr i8, ptr %a, i64 %i
  %bp = getelementptr i8, ptr %b, i64 %i
  %dp = getelementptr i8, ptr %dst, i64 %i
  %av = load <32 x i8>, ptr %ap, align 1    ; 256-bit load
  %bv = load <32 x i8>, ptr %bp, align 1
  %xv = xor <32 x i8> %av, %bv
  store <32 x i8> %xv, ptr %dp, align 1
  %next = add i64 %i, 32
  %cmp = icmp ult i64 %next, %len
  br i1 %cmp, label %loop, label %done
done:
  ret void
}
```

### 12.4 Small Buffer Optimization

Buffers <= 48 bytes could store data inline (avoiding a second heap allocation):

```
Threshold: 48 bytes (allows headers, UUIDs, hash digests to be single allocations)
Above threshold: separate malloc for data
```

This is a Phase 2+ optimization. The initial implementation always allocates data
separately for simplicity.

### 12.5 Memory Pressure and GC

The GC must track buffer memory separately from object count:

```c
// Trigger GC when total buffer memory exceeds threshold
static int64_t total_buffer_memory = 0;
static int64_t buffer_memory_threshold = 16 * 1024 * 1024;  // 16 MB

Buffer* alloc_buffer(int64_t size) {
    total_buffer_memory += size;
    if (total_buffer_memory > buffer_memory_threshold) {
        gc_collect();
        buffer_memory_threshold = total_buffer_memory * 2;  // adaptive
    }
    // ...
}
```

### 12.6 Zero-Copy Slicing (Future)

The initial design copies data on slice. A future optimization could use reference
counting or COW (copy-on-write) for slices that share underlying data:

```
// Future: slice returns a view (no copy)
var view = buf.view(4, 8)  // shares memory with buf
// Copy happens on first mutation of either buf or view
```

This adds complexity (reference counting, COW triggers) and is deferred to post-Phase-1.

---

## 13. Implementation Plan

### Phase 1: Bitwise Operators and Numeric Literals (~1 day)

**Status:** Mostly complete. Remaining work:

- [ ] Add `>>>` (unsigned right shift) token, parsing, codegen (`lshr`)
- [ ] Add binary literal (`0b`) parsing in lexer
- [ ] Add octal literal (`0o`) parsing in lexer
- [ ] Add numeric separator (`_`) support (optional, can defer)
- [ ] Tests for all bitwise operators and literal formats

**Files:**
- `src/compiler/lexer.sf` -- `read_number()` extension
- `src/compiler/parser.sf` -- `>>>` precedence
- `src/compiler/codegen/expr_body.sf` -- `lshr` emission for `>>>`
- `test/bitwise.sf` -- test file

### Phase 2: Buffer Type + Basic Access + File I/O (~2-3 days)

- [ ] Define `%Buffer` struct type in codegen
- [ ] Add `TAG_BUFFER` to NaN-boxing scheme
- [ ] Implement `__buffer_alloc`, `__buffer_length`, `__buffer_get_byte`, `__buffer_set_byte`
- [ ] Implement `__buffer_from_string`, `__buffer_from_list`, `__buffer_to_string`
- [ ] Implement `__buffer_fill`, `__buffer_copy`, `__buffer_slice`, `__buffer_equals`
- [ ] Implement `__buffer_concat`
- [ ] Implement `__buffer_to_hex`, `__buffer_from_hex`
- [ ] Implement `__buffer_to_base64`, `__buffer_from_base64`
- [ ] Implement `__buffer_read_file`, `__buffer_write_file`
- [ ] Register Buffer methods in codegen method dispatch
- [ ] Add `Buffer` to type system (type checker, `is` checks)
- [ ] GC integration (tracking, finalization)

**Files:**
- `runtime/buffer.c` (new) -- core Buffer implementation
- `runtime/buffer_io.c` (new) -- file I/O
- `runtime/value.h` -- add BUFFER tag
- `src/compiler/codegen/methods_body.sf` -- method dispatch
- `src/compiler/codegen/expr_body.sf` -- index get/set for Buffer
- `src/lib/buffer.sf` (new) -- stdlib wrapper module
- `test/buffer.sf` (new) -- tests

### Phase 3: Typed Read/Write Methods (~1-2 days)

- [ ] Implement all `__buffer_read_*` and `__buffer_write_*` runtime functions
- [ ] Expose via method dispatch in codegen
- [ ] Endianness detection (`Buffer.is_little_endian()`)
- [ ] Comprehensive tests for each width/endianness combination

**Files:**
- `runtime/buffer.c` -- add read/write functions
- `src/lib/buffer.sf` -- expose new methods
- `test/buffer_typed.sf` -- tests

### Phase 4: @struct Pack/Unpack (~1-2 days)

- [ ] Implement format string parser
- [ ] `Struct.pack` -- format + values -> Buffer
- [ ] `Struct.unpack` -- format + Buffer -> List
- [ ] `Struct.size` -- format -> byte count
- [ ] `Struct.layout` -- named layout constructor
- [ ] Layout `.pack()` and `.unpack()` methods

**Files:**
- `src/lib/struct.sf` (new) -- pure Saffron implementation using Buffer methods
- `runtime/struct.c` (new, optional) -- C fast path for hot format strings
- `test/struct.sf` (new) -- tests

### Phase 5: BufferReader / BufferWriter (~1 day)

- [ ] `Buffer.Reader` class with cursor, all read methods
- [ ] `Buffer.Writer` class with auto-growing buffer, all write methods
- [ ] Position management (seek, rewind, remaining, eof)
- [ ] Peek methods

**Files:**
- `src/lib/buffer.sf` -- add Reader/Writer classes
- `test/buffer_cursor.sf` -- tests

### Phase 6: Integration (~1 day)

- [ ] Update `@socket` to use Buffer instead of raw pointers
- [ ] Wire up `@encoding` module to accept/return Buffer
- [ ] Update any examples using raw pointer I/O
- [ ] Documentation and examples

**Files:**
- `src/lib/socket.sf` -- rewrite to use Buffer
- `src/lib/encoding.sf` -- Buffer-based API
- `examples/binary_file.sf` (new) -- example
- `examples/protocol_parser.sf` (new) -- example

### Total Estimated Effort: ~7-10 days

---

## 14. Design Decisions and Rationale

### 14.1 Why a Separate Type (Not `List<Int>`)?

Using `List<Int>` for bytes would be:
- **Memory-wasteful:** Each element is a 64-bit tagged value (8 bytes per byte stored)
- **Slow:** No contiguous byte access, no memcpy/memset optimization
- **Semantically wrong:** A byte buffer has different operations than a generic list

A dedicated `Buffer` type is 8x more memory-efficient and enables bulk optimizations.

### 14.2 Why Fixed-Size (Not Growable)?

Fixed-size buffers match the common case:
- Reading a file gives you a known-size buffer
- Protocol headers have fixed sizes
- Crypto operations produce fixed-size outputs

Growable semantics are provided by `BufferWriter`, which manages capacity internally
and produces a fixed-size Buffer on `.to_buffer()`. This separation avoids the
complexity of tracking capacity in every buffer and avoids unexpected reallocations.

### 14.3 Why Copy-on-Slice (Not Views)?

Copy-on-slice is simpler and safer:
- No dangling references if the parent buffer is collected
- No need for reference counting or COW machinery
- Mutations to a slice don't affect the original (no surprise aliasing)
- Matches Saffron's value semantics philosophy

The performance cost is acceptable for most use cases. Large buffers that need views
can use explicit offset tracking with a Reader.

### 14.4 Why Explicit Endianness (Not Default)?

Every read/write method includes `_le` or `_be` suffix because:
- Implicit endianness (like Java's always-big-endian DataInputStream) leads to
  subtle bugs when parsing little-endian formats (which are more common today)
- Explicit endianness is self-documenting: `read_u32_le` tells you exactly what
  byte order is expected
- Matches the approach of Rust (`from_le_bytes`), Go (`binary.LittleEndian`),
  and Node.js (`readUInt32LE`)

### 14.5 Why Python-Style Format Strings for @struct?

The format string approach:
- Is concise: `"<IHB"` vs. three separate read calls
- Is familiar: Python developers will recognize it immediately
- Enables static size calculation
- Works well for C struct interop where layouts are fixed

The named layout extension adds ergonomics without losing the format string's compactness.

---

## 15. Lessons from Other Languages

### 15.1 Rust: `bytes` Crate

Rust's `bytes` crate provides `Bytes` (immutable, reference-counted) and `BytesMut`
(mutable, uniquely owned). Key insight: separating immutable shared buffers from
mutable exclusive ones enables zero-copy networking.

**Lesson for Saffron:** Our `Buffer` is always mutable and owned. For networking
use cases, consider a future `Bytes` type that is immutable + ref-counted for
zero-copy passing between tasks.

### 15.2 Go: `[]byte` + `encoding/binary`

Go makes `[]byte` a slice of its fundamental byte array, giving it all slice
operations naturally. The `encoding/binary` package provides `Read`/`Write` with
explicit byte order. `bytes.Buffer` provides the growable writer.

**Lesson for Saffron:** Go's simplicity (just a byte slice with library functions)
is elegant. We adopt the same split: primitive type + library functions. The
`binary.Read` struct-reflection approach is powerful but requires reflection, which
Saffron doesn't have -- our format strings serve the same purpose.

### 15.3 Python: `struct` + `bytes` + `bytearray`

Python separates immutable `bytes` from mutable `bytearray`. The `struct` module's
format strings are the gold standard for binary packing. `io.BytesIO` provides the
cursor-based reader/writer.

**Lesson for Saffron:** Python's `struct` format string syntax is well-proven and
widely understood. We adopt it directly. We skip the mutable/immutable split (too
complex for a first version) and use a single mutable `Buffer` type.

### 15.4 Node.js: `Buffer`

Node.js `Buffer` is a Uint8Array subclass with convenience methods for reading/writing
at offsets. The API naming (`readUInt32LE`, `writeInt16BE`) is clear and systematic.

**Lesson for Saffron:** Node.js's method naming convention is excellent. We adopt
similar naming: `read_u32_le`, `write_i16_be`. The explicit offset parameter in
every call is clear but verbose -- our `BufferReader`/`BufferWriter` cursors solve
the verbosity for sequential access.

### 15.5 Zig: Comptime-Powered Generics

Zig uses comptime-evaluated generics to provide `std.mem.readInt(u32, buffer[0..4], .little)`.
The type system resolves the width at compile time.

**Lesson for Saffron:** Without dependent types or comptime, we cannot express this.
Our explicit `read_u32_le` / `read_u16_be` methods are the practical equivalent.

---

## 16. Open Questions

1. **Should `Buffer` support negative indexing?**
   - Pro: Consistent with List and String (`buf[-1]` = last byte)
   - Con: Adds a branch to every access
   - **Proposed:** Yes, for consistency. The branch is cheap and predictable.

2. **Should `Buffer.from_string` include the null terminator?**
   - Pro: Matches C convention, useful for FFI
   - Con: The null byte is not part of the string's content
   - **Proposed:** No null terminator. Provide `Buffer.from_cstring(s)` if needed for FFI
     that appends a zero byte.

3. **Should @struct format strings use compile-time validation?**
   - The format string is often a string literal. We could validate it at compile time
     and emit optimized code (no runtime parsing).
   - **Proposed:** Phase 1 uses runtime parsing. Phase 2 adds a compile-time optimization
     for literal format strings.

4. **Should `Buffer` be iterable?**
   - `for (byte in buf) { ... }` iterating over byte values
   - **Proposed:** Yes. Implement the iterator protocol (`.iter()`, `.has_next()`, `.next()`).
     Each iteration yields an `Int` (0-255).

5. **Should typed reads return `Int` or a new `U8`/`U16`/`U32` type?**
   - Saffron currently has only `Int` (64-bit signed). Adding unsigned integer types
     would be a much larger change.
   - **Proposed:** Return `Int` always. Document the value ranges. Provide masking
     utilities: `buf.read_u8(0)` returns 0-255, `buf.read_i8(0)` returns -128 to 127.
     Both are `Int` values.

6. **How should the Buffer type appear in error messages?**
   - **Proposed:** `Buffer` (simple). The `to_string()` for debug printing shows
     `<Buffer length=1024>` or hex dump for small buffers: `<Buffer 48 65 6c 6c 6f>`.

7. **Thread safety for async?**
   - With cooperative multitasking, a Buffer could be shared between tasks.
   - **Proposed:** No special thread safety. Buffers are not synchronized. If shared
     between tasks, the user is responsible for coordination (same as List/Map today).

---

## 17. Complete API Summary

### Buffer (import "@buffer")

```
// Construction
Buffer.alloc(size: Int): Buffer
Buffer.from_list(bytes: List<Int>): Buffer
Buffer.from_string(s: String): Buffer
Buffer.from_hex(s: String): Buffer
Buffer.from_base64(s: String): Buffer
Buffer.wrap(ptr: Int, length: Int): Buffer          // unsafe FFI
Buffer.concat_list(bufs: List<Buffer>): Buffer

// File I/O
Buffer.read_file(path: String): Buffer
Buffer.write_file(path: String, buf: Buffer)
Buffer.append_file(path: String, buf: Buffer)

// Utility
Buffer.is_little_endian(): Bool

// Instance methods
buf.length(): Int
buf[index]: Int                                     // get byte
buf[index] = value                                  // set byte
buf.slice(start: Int, end: Int): Buffer
buf.fill(value: Int, start?: Int, end?: Int)
buf.copy_from(src: Buffer, src_off: Int, dst_off: Int, len: Int)
buf.equals(other: Buffer): Bool
buf.concat(other: Buffer): Buffer

// Conversion
buf.to_string(): String
buf.to_string_lossy(): String
buf.to_list(): List<Int>
buf.to_hex(): String
buf.to_hex_upper(): String
buf.to_base64(): String
buf.to_base64_url(): String

// Typed reads
buf.read_u8(offset: Int): Int
buf.read_u16_le(offset: Int): Int
buf.read_u16_be(offset: Int): Int
buf.read_u32_le(offset: Int): Int
buf.read_u32_be(offset: Int): Int
buf.read_u64_le(offset: Int): Int
buf.read_u64_be(offset: Int): Int
buf.read_i8(offset: Int): Int
buf.read_i16_le(offset: Int): Int
buf.read_i16_be(offset: Int): Int
buf.read_i32_le(offset: Int): Int
buf.read_i32_be(offset: Int): Int
buf.read_i64_le(offset: Int): Int
buf.read_i64_be(offset: Int): Int
buf.read_f32_le(offset: Int): Float
buf.read_f32_be(offset: Int): Float
buf.read_f64_le(offset: Int): Float
buf.read_f64_be(offset: Int): Float

// Typed writes
buf.write_u8(offset: Int, value: Int)
buf.write_u16_le(offset: Int, value: Int)
buf.write_u16_be(offset: Int, value: Int)
buf.write_u32_le(offset: Int, value: Int)
buf.write_u32_be(offset: Int, value: Int)
buf.write_u64_le(offset: Int, value: Int)
buf.write_u64_be(offset: Int, value: Int)
buf.write_i8(offset: Int, value: Int)
buf.write_i16_le(offset: Int, value: Int)
buf.write_i16_be(offset: Int, value: Int)
buf.write_i32_le(offset: Int, value: Int)
buf.write_i32_be(offset: Int, value: Int)
buf.write_i64_le(offset: Int, value: Int)
buf.write_i64_be(offset: Int, value: Int)
buf.write_f32_le(offset: Int, value: Float)
buf.write_f32_be(offset: Int, value: Float)
buf.write_f64_le(offset: Int, value: Float)
buf.write_f64_be(offset: Int, value: Float)

// Bitwise bulk operations
buf.xor(other: Buffer): Buffer
buf.and_buf(other: Buffer): Buffer
buf.or_buf(other: Buffer): Buffer
buf.not_buf(): Buffer

// Iterator
buf.iter(): Iterator<Int>
```

### Buffer.Reader

```
Buffer.reader(buf: Buffer): Buffer.Reader

reader.read_u8(): Int
reader.read_u16_le(): Int
reader.read_u16_be(): Int
reader.read_u32_le(): Int
reader.read_u32_be(): Int
reader.read_u64_le(): Int
reader.read_u64_be(): Int
reader.read_i8(): Int
reader.read_i16_le(): Int
reader.read_i16_be(): Int
reader.read_i32_le(): Int
reader.read_i32_be(): Int
reader.read_i64_le(): Int
reader.read_i64_be(): Int
reader.read_f32_le(): Float
reader.read_f32_be(): Float
reader.read_f64_le(): Float
reader.read_f64_be(): Float
reader.read_string(len: Int): String
reader.read_bytes(len: Int): Buffer
reader.skip(n: Int)
reader.seek(offset: Int)
reader.rewind()
reader.position(): Int
reader.remaining(): Int
reader.eof(): Bool
reader.peek_u8(): Int
reader.peek_u16_le(): Int
reader.peek_u32_le(): Int
```

### Buffer.Writer

```
Buffer.writer(initial_capacity?: Int): Buffer.Writer

writer.write_u8(value: Int)
writer.write_u16_le(value: Int)
writer.write_u16_be(value: Int)
writer.write_u32_le(value: Int)
writer.write_u32_be(value: Int)
writer.write_u64_le(value: Int)
writer.write_u64_be(value: Int)
writer.write_i8(value: Int)
writer.write_i16_le(value: Int)
writer.write_i16_be(value: Int)
writer.write_i32_le(value: Int)
writer.write_i32_be(value: Int)
writer.write_i64_le(value: Int)
writer.write_i64_be(value: Int)
writer.write_f32_le(value: Float)
writer.write_f32_be(value: Float)
writer.write_f64_le(value: Float)
writer.write_f64_be(value: Float)
writer.write_string(s: String)
writer.write_bytes(buf: Buffer)
writer.write_zeros(n: Int)
writer.align(boundary: Int)
writer.to_buffer(): Buffer
writer.position(): Int
writer.capacity(): Int
```

### Struct (import "@struct")

```
Struct.pack(format: String, values: List<Any>): Buffer
Struct.unpack(format: String, buf: Buffer): List<Any>
Struct.size(format: String): Int

Struct.layout(fields: Map<String, String>, byte_order?: String): Struct.Layout

layout.pack(values: Map<String, Any>): Buffer
layout.unpack(buf: Buffer): Map<String, Any>
layout.size(): Int
layout.field_offset(name: String): Int

Struct.unpack_array(layout: Struct.Layout, buf: Buffer, count: Int): List<Map<String, Any>>
```

---

## 18. End-to-End Example: WASM Binary Parser

Demonstrates Buffer, BufferReader, @struct, and bitwise operators working together:

```saffron
import "@buffer" as Buffer
import "@struct" as Struct

// WASM magic number and version
var WASM_MAGIC: Int = 0x6D736100    // \0asm
var WASM_VERSION: Int = 0x01000000  // version 1

fun parse_wasm(path: String): Map<String, Any> {
    var data: Buffer = Buffer.read_file(path)
    var reader: Buffer.Reader = Buffer.reader(data)

    // Validate header
    var magic: Int = reader.read_u32_le()
    if (magic != 0x6D736100) {
        throw "Not a WASM file: bad magic number"
    }
    var version: Int = reader.read_u32_le()

    var sections: List<Map<String, Any>> = []

    // Parse sections
    while (!reader.eof()) {
        var section_id: Int = reader.read_u8()
        var section_size: Int = read_leb128(reader)
        var section_data: Buffer = reader.read_bytes(section_size)

        sections.push({
            "id": section_id,
            "size": section_size,
            "data": section_data
        })
    }

    return {
        "version": version,
        "sections": sections
    }
}

// LEB128 variable-length integer encoding (used throughout WASM)
fun read_leb128(reader: Buffer.Reader): Int {
    var result: Int = 0
    var shift: Int = 0
    var byte: Int = 0
    while (true) {
        byte = reader.read_u8()
        result = result | ((byte & 0x7F) << shift)
        if ((byte & 0x80) == 0) { break }
        shift = shift + 7
    }
    return result
}
```

This example uses:
- `Buffer.read_file` (file I/O)
- `Buffer.reader` (cursor-based reading)
- `reader.read_u32_le` / `reader.read_u8` (typed reads)
- `reader.read_bytes` (sub-buffer extraction)
- `&`, `|`, `<<` (bitwise operators for LEB128 decoding)
- `0x7F`, `0x80` (hex literals)
- `reader.eof()` (end detection)
