# The REPL

Launch the REPL by running the C VM binary with no arguments:

```bash
./cvm/cmake-build-debug/saffron
```

```
saffron v0.1 REPL
>>>
```

## Basic usage

Type expressions and statements directly:

```
>>> var x = 42
>>> IO.println(x * 2)
84
```

Variables, functions, and classes persist across lines within a session.

## Multi-line input

The REPL detects unclosed braces, parentheses, and brackets. It will prompt with `...` until the expression is complete:

```
>>> fun greet(name: String) {
...   IO.println("Hello, ${name}!")
... }
>>> greet("world")
Hello, world!
```

## Tips

- Use the REPL to test small expressions and explore the standard library
- Define helper functions to iterate quickly on an idea
- All imports work in the REPL just like in files
- The REPL uses the C VM interpreter (not the LLVM compiler), so it starts instantly
