# Interactive Use

**Saffron does not currently have a REPL.**

Earlier versions shipped one as part of the C bytecode interpreter. That interpreter now lives in `legacy/` and is no longer supported — it has drifted well behind the language and rejects modern syntax such as `@` annotations, the `Int` type, and `actor` declarations. Its REPL went with it.

## Iterating quickly instead

Compilation is fast enough that a scratch file plus `tools/saffron run` covers most of what a REPL is used for. Create a file:

```saffron
var greeting = "hello"
IO.println(greeting.to_upper())
IO.println([1, 2, 3].length())
```

And run it:

```bash
tools/saffron run scratch.sf
```

```
HELLO
3
```

Edit and re-run as you go. Because the whole file is type-checked on every run, you also get errors a line-at-a-time REPL would not catch until later.

## Exploring the standard library

`IO.println` works on scalars — numbers, strings, and booleans. To inspect a list or a map, loop over it and print the elements:

```saffron
var nums = [1, 2, 3, 4]
for (n in nums) {
    IO.println(n * n)
}
```

```
1
4
9
16
```

For a reference on what is available, see the [Standard Library](../stdlib/io.md) section.
