# Installation

## Building from source

Saffron is built with CMake and a C17 compiler.

### Requirements

- A C compiler supporting C17 (GCC 8+, Clang 5+, MSVC 2019+)
- CMake 3.16+

### Steps

```bash
git clone https://github.com/henry232323/saffron.git
cd saffron
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary is at `build/saffron`. You can move it anywhere on your `$PATH`:

```bash
sudo cp build/saffron /usr/local/bin/
```

### Verify

```bash
saffron --version
```
