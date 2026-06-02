# Unicode Support for Saffron

## Status

- **Stage:** Design / Proposal
- **Author:** —
- **Date:** 2026-06-02

---

## 1. Motivation

Saffron strings are currently byte arrays — null-terminated C strings with operations
that assume one byte equals one character. This works for ASCII but breaks in any
context involving real-world text:

- **Emoji:** `"hello 🎉".length()` returns 8 (4 bytes for the emoji), not 7 (6 chars + 1 emoji).
- **Internationalization:** `"café".char_at(3)` returns the first byte of a multi-byte
  `é` (0xC3), not the letter itself.
- **Case conversion:** `"straße".to_upper()` should yield `"STRASSE"` (German sharp-s),
  but byte-level `toupper()` does nothing.
- **Correctness:** `"नमस्ते".length()` returns 18 (UTF-8 bytes), not 6 codepoints or 4
  grapheme clusters (what a user would count as "characters").

Every modern language has confronted this. Saffron is pre-1.0 — now is the time to get
string semantics right before code depends on byte-level behavior.

### Design goals

1. Strings are **always valid UTF-8** at runtime.
2. User-facing operations default to **codepoint** semantics (with grapheme cluster APIs
   available for display/editing use cases).
3. **Zero conversion overhead** for I/O — files, network, and OS APIs already speak UTF-8.
4. **ASCII fast paths** preserve performance for the common case.
5. The `@unicode` stdlib module provides advanced queries (category, script, normalization)
   without bloating the core runtime.

---

## 2. String Representation Options

### 2.1 UTF-8 internally (Rust, Go, Zig)

Strings are byte arrays where the bytes happen to be valid UTF-8. Indexing by codepoint
is O(n) unless you cache offsets.

| Pros | Cons |
|------|------|
| Compact for ASCII and Latin scripts | O(n) random access by codepoint |
| No conversion on I/O (OS/files/network are UTF-8) | Multi-byte boundaries require care in slicing |
| Matches LLVM IR string representation already used | Need runtime validation on untrusted input |
| Industry momentum (Rust, Go, Swift backing store) | |

### 2.2 UTF-32 internally (early Python 3)

Every codepoint stored as a 32-bit integer. O(1) indexing.

| Pros | Cons |
|------|------|
| Trivial O(1) indexing | 4x memory for ASCII text |
| Simple implementation | Conversion needed on every I/O boundary |
| | Does not solve grapheme clusters (still multi-codepoint) |

### 2.3 Flexible/compact (Python PEP 393)

Store as Latin-1, UCS-2, or UCS-4 depending on the widest codepoint in the string.

| Pros | Cons |
|------|------|
| O(1) indexing | Complex allocator and bookkeeping |
| Compact for ASCII | Still needs conversion at I/O |
| | Significant implementation effort |

### 2.4 Recommendation: UTF-8

Saffron should use **UTF-8 as the sole internal representation**. This matches what the
LLVM compiler already emits (string constants are `[N x i8]` arrays), what the OS
provides, and what the C VM's `ObjString` already stores. The key change is semantic:
operations like `length()`, `char_at()`, and `slice()` will count/index **codepoints**
rather than bytes.

This is the same choice made by Rust (`str`), Go (`string`), and Swift (backing store),
and avoids the memory and conversion costs of wider encodings.

---

## 3. Impact on Existing String Methods

### 3.1 `length()` — codepoint count

**Current:** Returns `str->length` (byte count).
**New:** Returns the number of Unicode codepoints (scalar values) in the string.

```saffron
"hello".length()     // 5 (unchanged — pure ASCII)
"café".length()      // 4 (was 5 — 'é' is 2 bytes in UTF-8)
"🎉".length()        // 1 (was 4)
"नमस्ते".length()     // 6 codepoints (was 18 bytes)
```

**Implementation:** Walk the UTF-8 byte sequence, counting lead bytes. A lead byte
matches `(b & 0xC0) != 0x80`. For ASCII-only strings (flagged at creation), return
the byte length directly.

### 3.2 `char_at(i)` — codepoint access

**Current:** Returns a 1-byte string (`copyString(&str->chars[index], 1)`).
**New:** Returns a String containing the i-th codepoint (1-4 bytes of UTF-8).

```saffron
"café".char_at(3)    // "é" (the full 2-byte sequence)
"hello 🎉".char_at(6) // "🎉" (4 bytes, single codepoint)
```

**Implementation:** Seek forward `i` codepoints from the start, then copy the
multi-byte sequence of the i-th codepoint. O(n) in the general case; O(1) when
the ASCII flag is set.

### 3.3 `slice(start, end)` — codepoint range

**Current:** Byte offsets passed to `copyString(str->chars + start, end - start)`.
**New:** Codepoint-based start and end. Returns the substring spanning those codepoints.

```saffron
"café".slice(2, 4)    // "fé"
"hello 🎉!".slice(6, 7) // "🎉"
```

**Implementation:** Convert codepoint offsets to byte offsets by scanning from the
start of the string, then copy the byte range. Negative indices still supported
(relative to codepoint count).

### 3.4 `index_of(sub)` — codepoint offset

**Current:** Returns byte offset via `strstr()`.
**New:** Returns the codepoint offset of the first occurrence.

```saffron
"hello café".index_of("café")  // 6 (was 6 in ASCII, but matters for non-ASCII prefixes)
"αβγδ".index_of("γ")           // 2 (was 4 — each Greek letter is 2 bytes)
```

**Implementation:** Use `strstr()` to find the byte offset (unchanged since the
substring is also valid UTF-8), then convert byte offset to codepoint offset by
counting lead bytes in the prefix.

### 3.5 `to_upper()` / `to_lower()` — Unicode case mapping

**Current:** Byte-by-byte `toupper()`/`tolower()` — only handles ASCII a-z/A-Z.
**New:** Full Unicode case mapping using Unicode Character Database tables.

```saffron
"café".to_upper()    // "CAFE" (with proper É)
"straße".to_upper()  // "STRASSE" (sharp-s expands to two characters)
"İstanbul".to_lower() // "istanbul" (Turkish İ → i, with dot removed)
```

**Complexity:** Case mapping can change string length (ß → SS, fi ligature → FI).
The result must be a newly allocated string. Locale-dependent cases (Turkish İ/I)
can be handled by an optional locale parameter or deferred to `@unicode`.

**Initial implementation:** Use simple case mapping (1:1 codepoint mapping from
UnicodeData.txt `Simple_Uppercase_Mapping` / `Simple_Lowercase_Mapping`). Full
(conditional, locale-specific) case mapping lives in `@unicode`.

### 3.6 `split(delimiter)` — unchanged for valid UTF-8 delimiters

`strstr()`-based splitting works correctly on UTF-8 as long as both the string
and delimiter are valid UTF-8 (a substring match on bytes is equivalent to a
substring match on codepoints). No change needed.

```saffron
"α,β,γ".split(",")  // ["α", "β", "γ"] — works as-is
```

### 3.7 `contains(sub)`, `starts_with(prefix)`, `ends_with(suffix)` — unchanged

These use `strstr()` and `memcmp()` respectively. Since all operands are valid
UTF-8, byte-level comparison produces correct results. No change needed.

### 3.8 `replace(old, new)` — unchanged

`strstr()`-based replacement is correct for valid UTF-8 strings. The replacement
string may have a different byte length, which is already handled.

### 3.9 `trim()` — Unicode whitespace

**Current:** Uses `isspace()` which only recognizes ASCII whitespace (space, tab,
`\n`, `\r`, `\f`, `\v`).
**New:** Trim all Unicode whitespace characters:

- U+0009..U+000D (ASCII control characters)
- U+0020 (space)
- U+0085 (next line)
- U+00A0 (non-breaking space)
- U+1680 (Ogham space mark)
- U+2000..U+200A (en/em/thin/hair spaces)
- U+2028 (line separator)
- U+2029 (paragraph separator)
- U+202F (narrow no-break space)
- U+205F (medium mathematical space)
- U+3000 (ideographic space)

**Implementation:** Decode codepoints from both ends; check against a small lookup
table of Unicode whitespace codepoints.

### 3.10 `repeat(n)` — unchanged

Byte-level repetition produces correct UTF-8 output. No change.

### 3.11 `to_number()` — unchanged

`strtod()` already handles the full numeric input. No change.

---

## 4. New String Methods

### 4.1 Byte-level access

```saffron
"café".byte_length()   // 5 (4 ASCII bytes + 2-byte 'é')
"🎉".byte_length()     // 4
"hello".byte_length()  // 5 (same as length() for ASCII)

"café".bytes()         // iterator yielding: 99, 97, 102, 195, 169
```

`byte_length()` is O(1) — it returns the stored byte count directly (what
`length()` used to return).

`bytes()` returns an iterator over raw byte values as Numbers.

### 4.2 Codepoint iteration

```saffron
"café".codepoints()    // iterator yielding: 99, 97, 102, 233
"🎉".codepoints()      // iterator yielding: 127881

// Reconstruct from codepoints
var s = String.from_codepoints([99, 97, 102, 233])  // "café"
```

### 4.3 Grapheme cluster iteration

```saffron
// Family emoji: one grapheme cluster, 7 codepoints, 25 bytes
var family = "👨‍👩‍👧‍👦"
family.length()        // 7 (codepoints including ZWJs)
family.graphemes()     // iterator yielding: ["👨‍👩‍👧‍👦"] (single cluster)
family.grapheme_count() // 1

// Devanagari: combining marks form clusters
"नमस्ते".graphemes()    // ["न", "म", "स्", "ते"] — 4 clusters, 6 codepoints
```

Grapheme segmentation follows UAX #29 (Unicode Text Segmentation). This requires
Unicode property tables and is implemented in the `@unicode` module. The core
runtime provides `graphemes()` as a method that delegates to the `@unicode`
implementation when available, or falls back to codepoint-level iteration.

### 4.4 ASCII fast-path check

```saffron
"hello".is_ascii()     // true
"café".is_ascii()      // false

// Useful for algorithms that can use O(1) byte indexing when safe
if (input.is_ascii()) {
    // All codepoint operations become O(1) byte operations
    var ch = input.char_at(i)  // O(1)
}
```

### 4.5 Encoding conversion

```saffron
var bytes: List<Number> = "hello".encode("utf-8")    // [104, 101, 108, 108, 111]
var s = String.decode([0xC3, 0xA9], "utf-8")         // "é"

// Supported encodings (Phase 1): "utf-8", "ascii", "latin-1"
// Extended (via @unicode): "utf-16", "utf-32", "shift-jis", "euc-jp", etc.
```

### 4.6 Normalization

```saffron
// é can be: U+00E9 (precomposed) or U+0065 U+0301 (decomposed)
var precomposed = "\u{00E9}"           // "é" (NFC)
var decomposed = "\u{0065}\u{0301}"    // "é" (NFD)

precomposed == decomposed              // false (byte-level comparison)
precomposed.normalize("NFC") == decomposed.normalize("NFC")  // true

// Available forms: "NFC", "NFD", "NFKC", "NFKD"
```

### 4.7 Character classification (on String)

```saffron
"A".is_uppercase()     // true
"a".is_lowercase()     // true
"5".is_digit()         // true
" ".is_whitespace()    // true
"α".is_letter()        // true
"!".is_punctuation()   // true
```

These methods operate on the first codepoint of the string. They return `false` for
empty strings. For full Unicode property queries, use the `@unicode` module.

---

## 5. `@unicode` Stdlib Module

The `@unicode` module provides advanced Unicode operations that require shipping the
full Unicode Character Database. It is an opt-in import, keeping the core runtime small.

```saffron
import "@unicode" as Unicode

// Character properties
Unicode.category("A")         // "Lu" (Letter, uppercase)
Unicode.category("3")         // "Nd" (Number, decimal digit)
Unicode.category("!")         // "Po" (Punctuation, other)

// Property queries
Unicode.is_letter("α")       // true
Unicode.is_digit("٣")        // true (Arabic-Indic digit 3)
Unicode.is_whitespace("\u{00A0}")  // true (non-breaking space)
Unicode.is_control("\u{0000}")     // true
Unicode.is_symbol("$")       // true

// Character metadata
Unicode.name("🎉")           // "PARTY POPPER"
Unicode.block("中")          // "CJK Unified Ideographs"
Unicode.script("α")          // "Greek"
Unicode.script("中")         // "Han"
Unicode.age("🤖")            // "8.0" (Unicode version where it was introduced)

// Display width (for terminal/monospace rendering)
Unicode.width("A")           // 1
Unicode.width("中")          // 2 (full-width)
Unicode.width("\u{200B}")    // 0 (zero-width space)

// Normalization (also available as string method, implemented here)
Unicode.to_nfc("é")          // NFC normalized
Unicode.to_nfd("é")          // NFD normalized
Unicode.to_nfkc("ﬁ")        // "fi" (compatibility decomposition)
Unicode.to_nfkd("①")         // "1" (compatibility decomposition)

// Case folding (locale-independent equality comparison)
Unicode.case_fold("Straße")  // "strasse"
Unicode.case_fold("HELLO")   // "hello"

// Locale-aware case mapping
Unicode.to_upper("i", "tr")     // "İ" (Turkish locale)
Unicode.to_lower("I", "tr")     // "ı" (Turkish locale)
Unicode.to_upper("straße", "de") // "STRASSE"

// Grapheme segmentation (UAX #29)
Unicode.grapheme_boundaries("👨‍👩‍👧‍👦 hello")  // [0, 25, 26, 27, 28, 29, 30, 31]
Unicode.grapheme_count("👨‍👩‍👧‍👦 hello")       // 7

// Word/sentence segmentation
Unicode.word_boundaries("Hello, world!")     // [0, 5, 6, 7, 12, 13]
Unicode.sentence_boundaries("Hi. Bye.")     // [0, 4, 8]

// Bidirectional text
Unicode.bidi_class("A")      // "L" (left-to-right)
Unicode.bidi_class("א")      // "R" (right-to-left)

// Collation (locale-aware sorting)
Unicode.compare("café", "caff", "fr")  // -1 (é sorts before f in French)
```

### Module implementation pattern

Following the existing stdlib pattern (see `src/lib/iter.sf`, `src/lib/json.sf`), the
`@unicode` module will be a `.sf` file at `src/lib/unicode.sf` that calls into C
runtime functions for table lookups:

```saffron
// src/lib/unicode.sf

import "unicode_native" as _native

fun category(s: String): String {
    return _native.category(s)
}

fun is_letter(s: String): Bool {
    var cat: String = _native.category(s)
    return cat.starts_with("L")
}

fun width(s: String): Number {
    return _native.char_width(s)
}

// ... etc.
```

---

## 6. String Literals and `\u{XXXX}` Escapes

### 6.1 New escape syntax

Add `\u{XXXX}` (1-6 hex digits) for arbitrary Unicode codepoints:

```saffron
var heart = "\u{2764}"          // ❤
var emoji = "\u{1F389}"         // 🎉
var null_char = "\u{0}"         // U+0000 (stored as the codepoint, NOT a C null terminator)
var snowman = "\u{2603}"        // ☃
var max = "\u{10FFFF}"          // maximum valid codepoint
```

**Validation at compile time:** The parser rejects:
- Codepoints > U+10FFFF
- Surrogate halves (U+D800..U+DFFF) — these are not valid scalar values

### 6.2 Lexer changes

**C VM scanner (`cvm/scanner.c`):** In the `string()` function, after detecting `\\`,
add a case for `u`:

```c
case 'u':
    if (peek() == '{') {
        advance(); // skip {
        // Parse 1-6 hex digits
        uint32_t codepoint = 0;
        int digits = 0;
        while (peek() != '}' && !isAtEnd() && digits < 6) {
            char c = advance();
            codepoint = (codepoint << 4) | hexValue(c);
            digits++;
        }
        if (peek() == '}') advance(); // skip }
        // Encode as UTF-8 into the string buffer
        encodeUTF8(codepoint, buffer, &bufLen);
    }
    break;
```

**Self-hosted lexer (`src/compiler/lexer.sf`):** In `read_string()`, extend the escape
handling:

```saffron
else if (esc == "u") {
    if (this.peek() == "{") {
        this.advance() // skip {
        var hex: StringBuilder = StringBuilder()
        while (this.peek() != "}") {
            hex.append(this.advance())
        }
        this.advance() // skip }
        var codepoint: Number = parse_hex(hex.to_string())
        result.append(codepoint_to_utf8(codepoint))
    }
}
```

### 6.3 Multi-byte source files

The scanner already passes through multi-byte UTF-8 sequences in string literals
(it only looks for `"`, `\\`, `$`, and `\n` as special). No change needed for source
files containing non-ASCII characters in strings.

### 6.4 String interpolation

Template strings (`"hello ${expr}"`) work unchanged — the interpolated expression
produces a string value which is already valid UTF-8.

---

## 7. Runtime Implementation (LLVM-compiled path)

### 7.1 String representation at the IR level

No change to the IR representation. Strings remain pointers to null-terminated byte
arrays (`i8*`). The null terminator marks the end of the UTF-8 sequence. The GC
runtime tracks allocations.

**Important caveat:** To support U+0000 in strings (rare but valid), we may
eventually need a length-prefixed representation. For now, U+0000 is prohibited in
string content (same as C). This is acceptable for Phase 1.

### 7.2 New runtime C functions

These are linked into every compiled program via the runtime library:

```c
// UTF-8 codepoint counting (for length())
int64_t __str_codepoint_count(int64_t str_val);

// Codepoint-indexed char_at (returns a new string with one codepoint)
int64_t __str_char_at_cp(int64_t str_val, int64_t index);

// Codepoint-indexed slice
int64_t __str_slice_cp(int64_t str_val, int64_t start, int64_t end);

// Codepoint offset of substring (byte-find then convert)
int64_t __str_index_of_cp(int64_t str_val, int64_t sub_val);

// Unicode-aware case conversion (simple mapping)
int64_t __str_to_upper_unicode(int64_t str_val);
int64_t __str_to_lower_unicode(int64_t str_val);

// Unicode-aware trim
int64_t __str_trim_unicode(int64_t str_val);

// Byte length (O(1), just strlen)
int64_t __str_byte_length(int64_t str_val);

// ASCII check (scan for any byte > 127)
int64_t __str_is_ascii(int64_t str_val);

// UTF-8 validation
int64_t __str_is_valid_utf8(int64_t str_val);

// Codepoint iterator support
int64_t __str_codepoint_at_byte(int64_t str_val, int64_t byte_offset);
int64_t __str_next_byte_offset(int64_t str_val, int64_t byte_offset);
```

### 7.3 UTF-8 helper implementation

Core decoding logic (to be placed in `runtime/unicode.c`):

```c
// Decode one codepoint from a UTF-8 byte sequence.
// Returns the codepoint and advances *pos past its bytes.
static uint32_t decode_utf8(const uint8_t *s, int *pos) {
    uint8_t b = s[*pos];
    uint32_t cp;
    int len;
    if (b < 0x80)        { cp = b;          len = 1; }
    else if (b < 0xE0)   { cp = b & 0x1F;   len = 2; }
    else if (b < 0xF0)   { cp = b & 0x0F;   len = 3; }
    else                  { cp = b & 0x07;   len = 4; }
    for (int i = 1; i < len; i++) {
        cp = (cp << 6) | (s[*pos + i] & 0x3F);
    }
    *pos += len;
    return cp;
}

// Encode one codepoint as UTF-8 into buffer. Returns bytes written.
static int encode_utf8(uint32_t cp, uint8_t *buf) {
    if (cp < 0x80)        { buf[0] = cp;                                          return 1; }
    else if (cp < 0x800)  { buf[0] = 0xC0|(cp>>6); buf[1] = 0x80|(cp&0x3F);      return 2; }
    else if (cp < 0x10000){ buf[0] = 0xE0|(cp>>12); buf[1] = 0x80|((cp>>6)&0x3F);
                            buf[2] = 0x80|(cp&0x3F);                               return 3; }
    else                  { buf[0] = 0xF0|(cp>>18); buf[1] = 0x80|((cp>>12)&0x3F);
                            buf[2] = 0x80|((cp>>6)&0x3F); buf[3] = 0x80|(cp&0x3F); return 4; }
}
```

### 7.4 Codegen method dispatch changes

In `src/compiler/codegen/methods_body.sf`, the builtin string method dispatch
currently maps method names to `__str_*` runtime calls. The following mappings change:

| Method | Current runtime call | New runtime call |
|--------|---------------------|-----------------|
| `length` | inline (return stored byte count) | `__str_codepoint_count` |
| `char_at` | inline byte copy | `__str_char_at_cp` |
| `slice` | inline byte copy | `__str_slice_cp` |
| `index_of` | inline `strstr` | `__str_index_of_cp` |
| `to_upper` | inline `toupper` loop | `__str_to_upper_unicode` |
| `to_lower` | inline `tolower` loop | `__str_to_lower_unicode` |
| `trim` | inline `isspace` loop | `__str_trim_unicode` |

New method mappings to add:

| Method | Runtime call | Return type |
|--------|-------------|-------------|
| `byte_length` | `__str_byte_length` | `Int` |
| `is_ascii` | `__str_is_ascii` | `Bool` |
| `codepoints` | `__str_codepoints_iter` | `Iterator<Int>` |
| `bytes` | `__str_bytes_iter` | `Iterator<Int>` |
| `graphemes` | `__str_graphemes_iter` | `Iterator<String>` |
| `grapheme_count` | `__str_grapheme_count` | `Int` |
| `normalize` | `__str_normalize` | `String` |

---

## 8. Performance Considerations

### 8.1 ASCII fast path

The vast majority of strings in typical programs are pure ASCII. We can exploit this:

**Strategy:** Store an `is_ascii` flag in the string metadata. Set it at string
creation time by scanning for any byte > 0x7F. When the flag is true, all codepoint
operations become byte operations (O(1) indexing, length = byte_length).

For the C VM (`ObjString`), add a field:

```c
struct ObjString {
    Obj obj;
    int length;       // byte length (unchanged)
    int cpLength;     // codepoint length (cached, -1 = not yet computed)
    bool isAscii;     // true if all bytes < 0x80
    char *chars;
    uint32_t hash;
};
```

For the LLVM-compiled path, the runtime functions check the flag:

```c
int64_t __str_codepoint_count(int64_t str_val) {
    const char *s = val_to_ptr(str_val);
    // Fast path: scan for high bytes
    int len = strlen(s);
    for (int i = 0; i < len; i++) {
        if ((uint8_t)s[i] > 0x7F) goto slow_path;
    }
    return tag_int(len);

slow_path:
    int count = 0;
    int pos = 0;
    while (s[pos]) {
        if ((s[pos] & 0xC0) != 0x80) count++;
        pos++;
    }
    return tag_int(count);
}
```

### 8.2 O(n) indexing — documented trade-off

Codepoint-indexed `char_at(i)` and `slice(start, end)` are O(n) for non-ASCII
strings. This is an intentional trade-off:

- Most string processing is sequential (iteration, search, split) — not random access.
- Languages that chose O(1) indexing (Python with UCS-4 backing) pay 4x memory for
  ALL strings, even ASCII.
- Users who need O(1) access to specific positions should convert to a list of
  codepoints: `var cps = str.codepoints().to_list()`

**Documentation requirement:** The standard library docs must clearly state that
`char_at()` and `slice()` are O(n) for non-ASCII strings.

### 8.3 Codepoint length caching

For strings accessed repeatedly, cache the codepoint count after first computation.
In the C VM, the `cpLength` field stores this. In the LLVM path, consider a side
table or make `__str_codepoint_count` cache-friendly (likely not needed for Phase 1;
strings are typically processed once).

### 8.4 Grapheme cluster segmentation cost

Grapheme segmentation (UAX #29) requires:
- Unicode property tables (Grapheme_Cluster_Break values)
- A state machine over the break properties

This is non-trivial (~50KB of tables after compression). Keep it in `@unicode` only;
the core runtime does not ship grapheme tables. The `graphemes()` method on String
requires `import "@unicode"` to have been loaded, or returns codepoints as a fallback.

### 8.5 String comparison

Byte-level comparison (`memcmp`) remains correct for equality: two strings are equal
if and only if they have the same UTF-8 bytes. This is already how `==` works. For
locale-aware collation ordering, users should use `Unicode.compare()`.

### 8.6 String hashing

The existing hash function operates on bytes. Since we require strings to be in a
canonical byte form (no overlong sequences, etc.), equal strings always have equal
bytes, so hashing is correct. NFC/NFD normalization is NOT applied automatically —
users who need normalization-insensitive hashing should normalize before hashing.

---

## 9. Migration and Backwards Compatibility

### 9.1 Breaking changes

| Change | Impact |
|--------|--------|
| `length()` returns codepoint count | Code that uses `length()` for buffer sizing will break |
| `char_at(i)` returns multi-byte string | Code that assumes 1-byte result will break |
| `slice(i, j)` uses codepoint offsets | Code that computes byte offsets for slicing will break |
| `index_of()` returns codepoint offset | Code that uses result as byte offset will break |
| `to_upper()`/`to_lower()` may change string length | Code that assumes same-length result will break |
| `trim()` trims more whitespace chars | Minor — trims more aggressively |

### 9.2 Migration strategy

Since Saffron is pre-1.0, we take the breaking changes directly:

1. **No deprecation period.** Change semantics in one release.
2. **Provide `byte_length()`** as the escape hatch for code that needs byte counts.
3. **Document the migration** in release notes with before/after examples.
4. **Self-hosted compiler update:** The compiler itself uses `char_at()` extensively
   for single-byte character comparison (ASCII source code). Since source files are
   ASCII, the behavior is identical — no compiler changes needed.

### 9.3 Validation on string creation

All string creation paths must validate UTF-8:
- String literals: validated at compile time (the parser only emits valid UTF-8)
- `IO.read_file()`: validate at runtime, replace invalid bytes with U+FFFD or throw
- Network input: same as file I/O
- `String.decode(bytes, encoding)`: validates during decoding

Invalid UTF-8 input that cannot be validated should throw:

```saffron
try {
    var s = String.from_bytes([0xFF, 0xFE])  // invalid UTF-8
} catch (e) {
    IO.println("caught: ${e}")  // "Invalid UTF-8 sequence at byte 0"
}
```

---

## 10. Implementation Plan

### Phase 1: Foundation (lexer + validation + byte_length)

**Scope:**
- Add `\u{XXXX}` escape to both lexers (C VM scanner, self-hosted lexer)
- Add `byte_length()` method to strings (trivial — return existing byte count)
- Add `is_ascii()` method
- Add UTF-8 validation to `IO.read_file()` and string construction
- Update `ObjString` to cache `isAscii` flag

**Effort:** ~2 days. No breaking changes. Pure additions.

**Files touched:**
- `cvm/scanner.c` — `\u{XXXX}` in string scanning
- `cvm/libc/string.c` — add `byte_length`, `is_ascii` methods
- `cvm/object.h` / `cvm/object.c` — add `isAscii` flag to `ObjString`
- `src/compiler/lexer.sf` — `\u{XXXX}` in `read_string()`
- `src/compiler/codegen/methods_body.sf` — register new builtin methods
- `runtime/string.c` (new) — `__str_byte_length`, `__str_is_ascii` for LLVM path

### Phase 2: Codepoint-aware operations

**Scope:**
- Change `length()` to return codepoint count
- Change `char_at(i)` to codepoint indexing
- Change `slice(start, end)` to codepoint range
- Change `index_of()` to return codepoint offset
- Add `codepoints()` iterator
- Add `bytes()` iterator

**Effort:** ~3-4 days. Breaking change to existing semantics.

**Files touched:**
- `cvm/libc/string.c` — rewrite `stringLength`, `stringCharAt`, `stringSlice`,
  `stringIndexOf` using UTF-8 decoding
- `runtime/string.c` — add `__str_codepoint_count`, `__str_char_at_cp`,
  `__str_slice_cp`, `__str_index_of_cp`
- `src/compiler/codegen/methods_body.sf` — update dispatch to new runtime funcs

### Phase 3: Unicode-aware case + trim + `@unicode` module

**Scope:**
- Unicode case mapping for `to_upper()`/`to_lower()`
- Unicode whitespace for `trim()`
- Ship the `@unicode` module with category, name, script, block, width queries
- Build Unicode data tables (see Section 11)

**Effort:** ~1 week. Requires generating/shipping Unicode data tables.

**Files touched:**
- `cvm/libc/string.c` — rewrite `stringToUpper`, `stringToLower`, `stringTrim`
- `runtime/unicode.c` (new) — Unicode property lookups
- `runtime/unicode_tables.c` (generated) — compressed UCD tables
- `src/lib/unicode.sf` (new) — stdlib module
- `tools/gen_unicode_tables.py` (new) — script to generate tables from UCD

### Phase 4: Grapheme clusters + normalization + full module

**Scope:**
- UAX #29 grapheme cluster segmentation
- NFC/NFD/NFKC/NFKD normalization
- `graphemes()` method, `grapheme_count()`
- Word/sentence segmentation in `@unicode`
- Display width (`Unicode.width()`)
- Collation basics

**Effort:** ~1-2 weeks.

**Files touched:**
- `runtime/grapheme.c` (new) — grapheme break state machine
- `runtime/normalize.c` (new) — normalization algorithms
- `runtime/unicode_tables.c` — extended with grapheme break, canonical combining class,
  decomposition mapping tables
- `src/lib/unicode.sf` — extended API

---

## 11. Unicode Data Tables

### 11.1 Data source

The Unicode Character Database (UCD) from unicode.org, currently at version 15.1.
Key files:

- `UnicodeData.txt` — codepoint names, categories, case mappings
- `CaseFolding.txt` — case folding rules
- `GraphemeBreakProperty.txt` — grapheme cluster boundaries
- `EastAsianWidth.txt` — display width
- `Scripts.txt` — script assignment
- `Blocks.txt` — block ranges
- `DerivedCoreProperties.txt` — derived properties

### 11.2 Table compression

Full UCD is ~1.8MB uncompressed. For an embedded language, we need compression:

**Two-stage table (trie):** The standard approach for Unicode property lookup.
Codepoints are split into high/low parts (e.g., high 8 bits index into a stage-1
table of block pointers, low 8 bits index into the block). Blocks with identical
content are deduplicated.

Typical sizes after compression:
- General category: ~7KB
- Simple case mapping: ~5KB  
- Grapheme break property: ~6KB
- East Asian width: ~4KB
- Total for Phase 3: ~20KB
- Total for Phase 4: ~40KB

### 11.3 Build-time generation

A Python script (`tools/gen_unicode_tables.py`) downloads and parses UCD files,
generates C source with static arrays:

```c
// Generated by tools/gen_unicode_tables.py from Unicode 15.1
// Do not edit manually.

#include "unicode_tables.h"

// Stage-1 table: maps high byte to block index
static const uint8_t cat_stage1[256] = { ... };

// Stage-2 blocks: each block covers 256 codepoints
static const uint8_t cat_stage2[][256] = { ... };

UnicodeCategory unicode_category(uint32_t cp) {
    if (cp > 0x10FFFF) return UC_UNASSIGNED;
    uint8_t block = cat_stage1[cp >> 8];
    return (UnicodeCategory)cat_stage2[block][cp & 0xFF];
}
```

For the LLVM-compiled path, these tables become a `unicode_data.o` file linked
into the final binary. Programs that don't import `@unicode` can skip linking
these tables (reducing binary size).

### 11.4 Version management

The Unicode version is pinned at build time. A constant `UNICODE_VERSION` is
exposed via `Unicode.version()`. The generation script records which UCD version
it used.

---

## 12. Lessons from Other Languages

### 12.1 Rust: `char` vs `str`

Rust distinguishes `char` (a Unicode scalar value, 4 bytes) from string slices
(`&str`, UTF-8 bytes). Indexing into a `&str` by byte offset requires explicit
`.chars().nth(i)` for codepoint access.

**Lesson:** Exposing byte offsets to users causes confusion and bugs. Saffron
should default to codepoint semantics and make byte access explicit.

### 12.2 Swift: `Character` = extended grapheme cluster

Swift's `String` indexes by `Character` (grapheme cluster) by default. This means
`"café".count == 4` regardless of whether `é` is precomposed or decomposed. However,
all indexing is O(n) and index types are opaque (not integers).

**Lesson:** Grapheme-level default semantics are the most correct for "what users
see" but add significant complexity. Saffron takes the middle ground: codepoint
by default, graphemes via explicit API.

### 12.3 Python 3: transparent codepoint strings

Python strings are sequences of codepoints. `len("café") == 4`, `"café"[3] == "é"`.
Internally uses PEP 393 flexible representation. Clean API, hides complexity.

**Lesson:** Codepoint-level semantics are intuitive and widely understood. Python's
model is a good target for Saffron's user-facing behavior.

### 12.4 Java/JavaScript: UTF-16 legacy

Java `char` and JavaScript strings are UTF-16 code units. Characters outside the BMP
(emoji, CJK Extension B) appear as surrogate pairs. `"🎉".length == 2` in JavaScript.

**Lesson:** Never expose encoding internals as the default abstraction. This is a
historical mistake we avoid by using codepoints as the unit.

### 12.5 Go: byte-level with explicit rune iteration

Go strings are byte slices. `len("café")` returns bytes (5). Codepoint iteration
requires `for _, r := range s` or `utf8.DecodeRune()`.

**Lesson:** Byte-default is pragmatic for systems programming but confusing for
application developers. Saffron targets application use cases; codepoint default
is more appropriate.

---

## 13. Open Questions

1. **Should string comparison use NFC normalization by default?**
   - Argument for: `"e\u{0301}" == "\u{E9}"` would be true (user expectation)
   - Argument against: Normalization on every comparison is expensive; explicit is better
   - **Proposed answer:** No. Byte-equality by default; provide `Unicode.case_fold()`
     and `.normalize()` for canonical comparison.

2. **How to handle invalid UTF-8 from external sources?**
   - Option A: Replace invalid bytes with U+FFFD (lenient, like Python `errors='replace'`)
   - Option B: Throw an error (strict, forces handling)
   - **Proposed answer:** Default to strict (throw). Provide `IO.read_file_lossy()`
     for the lenient path.

3. **Should we support U+0000 (null) in strings?**
   - Currently impossible due to null-terminated representation.
   - **Proposed answer:** Defer. Require length-prefixed strings (Phase 5+). For now,
     document that `\u{0}` is not supported.

4. **Grapheme clusters as the default unit (Swift model)?**
   - **Proposed answer:** No. Too expensive for the default. Codepoints are the right
     middle ground — they match what most developers expect and what other languages
     (Python, Rust's `.chars()`) provide.

5. **Should `==` on strings be normalization-aware?**
   - **Proposed answer:** No. Byte-level equality. Normalization is a conscious choice.
     This matches Rust, Go, Python, and every other major language.

---

## 14. Summary of API

### Core string methods (always available)

```
str.length()         -> Int      // codepoint count
str.byte_length()    -> Int      // byte count (O(1))
str.char_at(i)       -> String   // i-th codepoint as string
str.slice(start, end)-> String   // codepoint range
str.index_of(sub)    -> Int      // codepoint offset (-1 if not found)
str.contains(sub)    -> Bool     // unchanged
str.starts_with(s)   -> Bool     // unchanged
str.ends_with(s)     -> Bool     // unchanged
str.split(delim)     -> List<String>  // unchanged
str.replace(old,new) -> String   // unchanged
str.trim()           -> String   // Unicode whitespace
str.to_upper()       -> String   // Unicode simple uppercase
str.to_lower()       -> String   // Unicode simple lowercase
str.repeat(n)        -> String   // unchanged
str.to_number()      -> Number   // unchanged
str.is_ascii()       -> Bool     // true if all bytes < 0x80
str.bytes()          -> Iterator<Int>   // raw byte values
str.codepoints()     -> Iterator<Int>   // codepoint values
str.graphemes()      -> Iterator<String> // grapheme clusters
str.grapheme_count() -> Int      // number of grapheme clusters
str.normalize(form)  -> String   // NFC/NFD/NFKC/NFKD
str.encode(enc)      -> List<Int> // encode to byte list
String.decode(bytes, enc)  -> String  // decode from byte list
String.from_codepoints(l)  -> String  // construct from codepoint list
```

### `@unicode` module (opt-in import)

```
Unicode.category(s)      -> String   // "Lu", "Nd", etc.
Unicode.name(s)          -> String   // "LATIN SMALL LETTER A"
Unicode.block(s)         -> String   // "Basic Latin"
Unicode.script(s)        -> String   // "Latin"
Unicode.width(s)         -> Int      // 0, 1, or 2
Unicode.is_letter(s)     -> Bool
Unicode.is_digit(s)      -> Bool
Unicode.is_whitespace(s) -> Bool
Unicode.is_uppercase(s)  -> Bool
Unicode.is_lowercase(s)  -> Bool
Unicode.case_fold(s)     -> String
Unicode.to_upper(s,loc)  -> String   // locale-aware
Unicode.to_lower(s,loc)  -> String   // locale-aware
Unicode.to_nfc(s)        -> String
Unicode.to_nfd(s)        -> String
Unicode.to_nfkc(s)       -> String
Unicode.to_nfkd(s)       -> String
Unicode.compare(a,b,loc) -> Int      // collation
Unicode.version()        -> String   // "15.1.0"
Unicode.grapheme_boundaries(s) -> List<Int>
Unicode.word_boundaries(s)     -> List<Int>
Unicode.bidi_class(s)    -> String
```

---

## 15. File Encoding Support

Once Saffron strings are UTF-8 by default, we need explicit encoding support for reading/writing files in other encodings (Windows-1252, Shift_JIS, ISO-8859-1, UTF-16, etc.).

### Design Principle

All in-memory strings remain UTF-8. Encoding/decoding happens only at I/O boundaries: when reading bytes from a file into a string, or writing a string out to bytes in a target encoding.

### API: `@encoding` Module

```saffron
import "@encoding" as Encoding

// Read a file in a non-UTF-8 encoding
var content: String = Encoding.read_file("data.csv", "windows-1252")

// Write a string to a file in a specific encoding
Encoding.write_file("output.txt", content, "shift_jis")

// Low-level: decode raw bytes to UTF-8 string
var bytes: List<Number> = IO.read_bytes("legacy.dat")
var text: String = Encoding.decode(bytes, "iso-8859-1")

// Low-level: encode a UTF-8 string to bytes in target encoding
var encoded: List<Number> = Encoding.encode(content, "utf-16le")
IO.write_bytes("output.bin", encoded)

// List available encodings
var available: List<String> = Encoding.list_encodings()
// ["utf-8", "utf-16le", "utf-16be", "utf-32le", "utf-32be",
//  "ascii", "iso-8859-1", "windows-1252", "shift_jis", "euc-jp",
//  "gb2312", "big5", "koi8-r", ...]

// Check if an encoding is supported
Encoding.is_supported("windows-1252")  // true

// Detect encoding from BOM or heuristics (best-effort)
var detected: String = Encoding.detect(bytes)  // "utf-8", "utf-16le", etc.
```

### Encoding Error Handling

```saffron
// Strict mode (default): throw on unmappable characters
Encoding.encode("hello 🎉", "ascii")
// throws: "Encoding: character U+1F389 cannot be represented in ascii"

// Replace mode: substitute unmappable chars
var opts: Map<String, String> = {"errors": "replace", "replacement": "?"}
var bytes: List<Number> = Encoding.encode_with("hello 🎉", "ascii", opts)
// bytes represent "hello ?"

// Ignore mode: skip unmappable chars
var opts2: Map<String, String> = {"errors": "ignore"}
var bytes2: List<Number> = Encoding.encode_with("hello 🎉", "ascii", opts2)
// bytes represent "hello "
```

### IO Module Changes

The existing `IO.read_file` and `IO.write_file` assume UTF-8 (which is correct for the default). For encoding-aware I/O, users import `@encoding`:

```saffron
// These remain UTF-8 (no change to existing API)
IO.read_file("file.txt")         // reads bytes, assumes UTF-8
IO.write_file("file.txt", data)  // writes UTF-8 bytes

// For other encodings, use @encoding
Encoding.read_file("legacy.csv", "windows-1252")
Encoding.write_file("output.csv", data, "windows-1252")

// Binary I/O (raw bytes, no encoding)
IO.read_bytes("image.png")       // List<Number>
IO.write_bytes("out.bin", bytes) // List<Number>
```

### Supported Encodings (Phase 1)

| Encoding | Aliases | Notes |
|----------|---------|-------|
| utf-8 | utf8 | Default, identity transform |
| utf-16le | utf16le, utf-16 | Windows native |
| utf-16be | utf16be | Network byte order |
| utf-32le | utf32le | Fixed-width |
| utf-32be | utf32be | Fixed-width |
| ascii | us-ascii | 7-bit only |
| iso-8859-1 | latin1, latin-1 | Western European |
| windows-1252 | cp1252 | Windows Western |

### Supported Encodings (Phase 2 — CJK + Cyrillic)

| Encoding | Aliases | Notes |
|----------|---------|-------|
| shift_jis | shift-jis, sjis | Japanese |
| euc-jp | eucjp | Japanese Unix |
| iso-2022-jp | | Japanese email |
| gb2312 | gbk, gb18030 | Chinese simplified |
| big5 | big5-hkscs | Chinese traditional |
| euc-kr | | Korean |
| koi8-r | | Russian |
| windows-1251 | cp1251 | Cyrillic Windows |

### Implementation

Encoding tables can be:
1. **Compiled into the runtime** as static byte arrays (for common encodings like latin1, windows-1252 — just 256-entry lookup tables)
2. **Generated at build time** from Unicode consortium data (for complex multi-byte encodings like Shift_JIS, GB2312)
3. **Linked from system iconv** as a fallback for exotic encodings (optional, not required)

For the LLVM-compiled path, encoding functions are C runtime calls:
```c
// runtime/encoding.c
int64_t __encoding_decode(int64_t bytes_list, int64_t encoding_str);  // -> String
int64_t __encoding_encode(int64_t string, int64_t encoding_str);      // -> List<Number>
```

### BOM Handling

```saffron
// Write UTF-8 with BOM (some Windows tools expect this)
Encoding.write_file_with_bom("output.txt", content, "utf-8")

// Read with BOM detection and stripping
var result = Encoding.read_file_auto("unknown.txt")
// Checks for BOM: UTF-8 (EF BB BF), UTF-16LE (FF FE), UTF-16BE (FE FF)
// Falls back to UTF-8 if no BOM detected
```

### Streaming / Large Files

For large files where loading everything into memory is impractical:

```saffron
var reader = Encoding.open_reader("huge.csv", "windows-1252")
while (reader.has_next()) {
    var line: String = reader.read_line()  // decoded to UTF-8
    // process line
}
reader.close()

var writer = Encoding.open_writer("output.csv", "shift_jis")
writer.write_line("ヘッダー,データ")
writer.close()
```
