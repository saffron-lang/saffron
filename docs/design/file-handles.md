# File Handles for @io

## Problem

The IO module only supports one-shot file operations (`read_file`, `write_file`, `append_file`). There is no way to open a file, read incrementally, seek within it, or do binary I/O through a persistent handle.

## Design

Add a `File` class to `@io` that wraps a C `FILE*` pointer via `@extern` functions.

### Opening and closing

```saffron
import "@io" as IO

var f = IO.open("data.txt", "r")   // open for reading
var out = IO.open("out.bin", "wb") // open for writing binary
f.close()
```

Modes: `"r"`, `"w"`, `"a"`, `"rb"`, `"wb"`, `"ab"`, `"r+"`, `"w+"`.

### Text reading

```saffron
f.read(1024)       // read up to N bytes as String (returns "" at EOF)
f.read_line()      // read one line including \n (returns nil at EOF)
f.read_all()       // read remaining content as String
```

### Text writing

```saffron
f.write("hello")        // write string, return bytes written
f.write_line("hello")   // write string + \n
```

### Binary I/O (Buffer integration)

```saffron
import "@bytes" as Bytes

var buf: Bytes.Buffer = f.read_bytes(256)   // read N bytes into Buffer
f.write_bytes(buf)                          // write Buffer contents
```

Open in `"rb"`/`"wb"` mode for binary. Text mode may translate line endings on some platforms.

### Seeking and position

```saffron
f.seek(offset)          // seek from start
f.seek_end(offset)      // seek from end (offset typically negative)
f.tell()                // current byte offset
f.rewind()              // equivalent to seek(0)
```

### Properties and control

```saffron
f.is_open(): Bool
f.path(): String
f.mode(): String
f.size(): Int           // file size (seeks to end, restores position)
f.flush()
f.close()
```

### Line iterator

```saffron
for (line in f.lines()) {
    IO.println(line.trim())
}
```

`lines()` returns an object implementing the iterator protocol (`iter()`, `has_next()`, `next()`). Each call to `next()` returns the next line (with trailing `\n` stripped).

### Standard streams

```saffron
IO.stdin     // pre-opened File (mode "r")
IO.stdout    // pre-opened File (mode "w")
IO.stderr    // pre-opened File (mode "w")

IO.stdin.read_line()
IO.stderr.write("error: something broke\n")
```

These are module-level `File` instances initialized from C's `stdin`/`stdout`/`stderr`.

## Error handling

| Operation | On failure |
|-----------|-----------|
| `IO.open(path, mode)` | Throws `"IO: cannot open file: <path>"` |
| Read on closed file | Throws `"IO: file is closed"` |
| Write on closed file | Throws `"IO: file is closed"` |
| Write on read-only file | Throws `"IO: file not writable"` |
| `read()` at EOF | Returns `""` |
| `read_line()` at EOF | Returns `nil` |
| `read_bytes(n)` at EOF | Returns Buffer with 0 length |

## Resource management

Files must be manually closed with `f.close()`. The language does not yet have `with` blocks or `defer`. A GC-triggered finalizer will attempt to close leaked handles (best-effort, not guaranteed).

Future options:
- `with IO.open("f.txt", "r") as f { ... }` (auto-close on block exit)
- `defer f.close()` (auto-close on scope exit)

Neither exists today. Document the manual-close requirement clearly in stdlib docs.

## Implementation plan

### Phase 1: Core (text mode)

Add C externs wrapping libc:

```
@extern("i64 __io_fopen(i64, i64)")    fun _fopen(path: Int, mode: Int): Int
@extern("void __io_fclose(i64)")        fun _fclose(handle: Int)
@extern("i64 __io_fread(i64, i64)")     fun _fread(handle: Int, n: Int): Int
@extern("i64 __io_fgets(i64)")          fun _fgets(handle: Int): Int
@extern("i64 __io_fwrite(i64, i64)")    fun _fwrite(handle: Int, data: Int): Int
@extern("void __io_fflush(i64)")        fun _fflush(handle: Int)
```

The File class stores the `FILE*` as an `Int` (i64 pointer cast). Every method checks `this._handle != 0` before operating.

Implement the `File` class, `IO.open()`, and `IO.stdin`/`IO.stdout`/`IO.stderr` in `src/lib/io.sf`.

Runtime support functions go in `src/runtime/io_runtime.c` (or equivalent) and link via the existing extern mechanism.

### Phase 2: Seeking

Add `fseek`/`ftell` externs. Implement `seek()`, `seek_end()`, `tell()`, `rewind()`, `size()`.

### Phase 3: Binary I/O

Add `read_bytes(n)` and `write_bytes(buf)`. These construct/consume `Buffer` objects from `@bytes`. Requires `import "@bytes"` inside `io.sf` (circular import check: io does not currently import bytes, this is fine).

### Phase 4: Iterator and streams

Implement `LineIterator` class with `iter()`, `has_next()`, `next()`. Wire up `IO.stdin`/`stdout`/`stderr` from special-cased handle values (0/1/2 mapped to C's standard streams).

### Phase 5: Encoding (future, depends on @encoding)

Add optional `encoding` parameter to `IO.open()`. Default is `"utf-8"` (passthrough). Non-UTF-8 encodings require an `@encoding` module that does not yet exist.

## Compatibility

- Existing `IO.read_file`/`write_file`/`append_file`/`file_exists` remain unchanged as convenience wrappers.
- The `File` class is additive; no breaking changes to the module.
- The `@bytes` Buffer type is used as-is for binary operations.

## Open questions

1. Should `read_line()` return nil or `""` at EOF? Nil is clearer for loop termination (`while (line != nil)`), but requires the caller to handle Option types. Recommendation: return nil.
2. Should `size()` be a method (seeks internally) or a property cached at open time? Method is safer (file can grow). Property would be stale.
3. Should `lines()` strip the trailing `\n`? Recommendation: yes, strip it. Raw access is available via `read_line()` which preserves it.
