# Installation

Saffron has two runtimes: a **C VM** (interpreter, REPL) and an **LLVM native compiler** (ahead-of-time compilation to native binaries).

## Building from source

### Requirements

- A C compiler supporting C17 (GCC 8+, Clang 5+)
- CMake 3.16+ (for the C VM)
- LLVM/Clang toolchain (for the native compiler backend)

### The C VM (interpreter and REPL)

```bash
git clone https://github.com/henry232323/saffron.git
cd saffron
cmake -B cvm/cmake-build-debug -S cvm
cmake --build cvm/cmake-build-debug
```

The interpreter binary is at `cvm/cmake-build-debug/saffron`. Run a file or launch the REPL:

```bash
./cvm/cmake-build-debug/saffron              # REPL
./cvm/cmake-build-debug/saffron program.sf   # run a file
```

### The native compiler (LLVM backend)

The native compiler bootstraps itself. A pre-compiled gen2 binary is checked in at `build/stage2/saffronc`.

```bash
./bootstrap.sh
```

This produces `build/saffronc` (the gen3 compiler). Use the `tools/saffron` driver for a unified interface:

```bash
tools/saffron run program.sf           # compile + link + run
tools/saffron build program.sf -o app  # compile to native binary
tools/saffron emit-ir program.sf       # output LLVM IR
```

### Verify

```bash
tools/saffron run test/hello_bootstrap.sf
```

## Which runtime to use?

| Use case | Runtime |
|----------|---------|
| Interactive exploration, REPL | C VM (`cvm/cmake-build-debug/saffron`) |
| Running scripts quickly | C VM |
| Building native binaries | LLVM compiler (`tools/saffron build`) |
| WebAssembly targets | LLVM compiler with `--target wasm32` |
| Performance-critical code | LLVM compiler (produces optimized native code) |

Both runtimes support the same language — the difference is execution model (interpreted bytecode vs compiled native code).
