# Installation

Saffron is a **self-hosted compiler**: the compiler is written in Saffron and compiles itself. It emits LLVM IR, which is then linked into a native binary or a WebAssembly module.

## Building from source

### Requirements

- LLVM/Clang toolchain (used to assemble and link the emitted IR)
- A POSIX shell (the bootstrap and driver scripts are Bash)

### Bootstrap the compiler

A pre-compiled gen2 compiler is checked in at `build/stage2/saffronc`. Bootstrapping uses it to compile the current compiler source into gen3:

```bash
git clone https://github.com/saffron-lang/saffron.git
cd saffron
./bootstrap.sh
```

This produces `build/saffronc` — the gen3 compiler, and the one everything else uses.

### Verify

```bash
tools/saffron run test/hello_bootstrap.sf
```

You should see:

```
Hello from Saffron 0.1.0!
Bootstrapped successfully.
The compiler compiled itself. We're self-hosting!
```

## Using the compiler

The `tools/saffron` driver wraps the compiler and the linker behind one command:

```bash
tools/saffron run program.sf                        # compile + link + run
tools/saffron build program.sf -o app               # compile to a native binary
tools/saffron build program.sf --target wasm32 -o app.wasm  # compile to WebAssembly
tools/saffron emit-ir program.sf                    # print LLVM IR to stdout
```

To invoke the compiler directly and stop at LLVM IR:

```bash
build/saffronc program.sf program.ll
```

## Which command to use?

| Use case | Command |
|----------|---------|
| Running a script | `tools/saffron run program.sf` |
| Shipping a standalone binary | `tools/saffron build program.sf -o app` |
| WebAssembly targets | `tools/saffron build program.sf --target wasm32 -o app.wasm` |
| Inspecting generated code | `tools/saffron emit-ir program.sf` |

## A note on the C VM

Early versions of Saffron ran on a bytecode interpreter written in C, which also provided a REPL. That interpreter now lives in `legacy/` and is **no longer supported** — it has drifted well behind the language and rejects modern syntax such as `@` annotations, the `Int` type, and `actor` declarations. Use the self-hosted compiler for everything.
