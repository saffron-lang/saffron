# Hello World

Create a file called `hello.sf`:

```saffron
IO.println("Hello, world!")
```

Run it:

```bash
tools/saffron run hello.sf
```

Output:

```
Hello, world!
```

Or compile it to a standalone binary:

```bash
tools/saffron build hello.sf -o hello
./hello
```

## What just happened?

`IO` is a built-in module available in every Saffron program — no import needed. `println` prints a value followed by a newline.

## String interpolation

Saffron supports `${}` interpolation inside double-quoted strings:

```saffron
var name = "Saffron"
var version = 0.1
IO.println("Welcome to ${name} v${version}!")
```

Output:

```
Welcome to Saffron v0.1!
```

Any expression works inside `${}`:

```saffron
IO.println("2 + 2 = ${2 + 2}")
```

## Next steps

Jump into the [tutorial](../tutorial/variables-and-types.md), or see [Interactive Use](./repl.md) for tips on iterating quickly.
