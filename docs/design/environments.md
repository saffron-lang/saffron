# Build Environments

## Status: Proposal
## Date: 2026-06-03

---

## 1. Overview

Build environments define compilation profiles (dev, test, prod) that control optimization levels, debug info, stripping, LTO, and environment variables. This allows a single `pantry.toml` to describe how the project should be built for different contexts.

---

## 2. Configuration

### `pantry.toml` environment sections

```toml
[environments.dev]
optimization = 0
debug_info = true
strip = false
lto = false

[environments.dev.env-vars]
LOG_LEVEL = "debug"
DEBUG = "1"

[environments.test]
optimization = 0
debug_info = true
strip = false
lto = false

[environments.test.env-vars]
LOG_LEVEL = "debug"
TEST_MODE = "1"

[environments.prod]
optimization = 2
debug_info = false
strip = true
lto = true

[environments.prod.env-vars]
LOG_LEVEL = "warn"
```

### Fields

| Field | Type | Values | Description |
|-------|------|--------|-------------|
| `optimization` | Integer | 0-3 | Maps to clang `-O0` through `-O3` |
| `debug_info` | Bool | true/false | Emit DWARF debug info (`-g`) |
| `strip` | Bool | true/false | Strip symbols from binary (`--strip`) |
| `lto` | Bool | true/false | Enable link-time optimization (`-flto`) |

### Defaults

If no `[environments]` section exists, pantry uses implicit defaults:

| Environment | optimization | debug_info | strip | lto |
|-------------|-------------|------------|-------|-----|
| dev | 0 | true | false | false |
| test | 0 | true | false | false |
| prod | 2 | false | true | true |

Custom environments can be defined with any name:

```toml
[environments.bench]
optimization = 3
debug_info = false
strip = false
lto = true
```

---

## 3. CLI Usage

```bash
pantry build                    # uses 'dev' environment (default)
pantry build --env=prod         # uses 'prod' environment
pantry build --env=bench        # uses custom 'bench' environment
pantry test                     # auto-selects 'test' environment
pantry install --env=prod       # install with prod flags
```

The `--env` flag is accepted by `build`, `install`, `run`, and `test` commands. `pantry test` defaults to the `test` environment unless overridden.

---

## 4. Dependency Scoping

### `[dev-dependencies]`

Included in `dev` and `test` environments, excluded from `prod`:

```toml
[dependencies]
http = "^0.3.0"

[dev-dependencies]
mock = "^1.0.0"
bench = "^0.2.0"

[test-dependencies]
fixtures = { path = "../test-fixtures" }
```

| Dependency section | dev | test | prod |
|-------------------|-----|------|------|
| `[dependencies]` | yes | yes | yes |
| `[dev-dependencies]` | yes | yes | no |
| `[test-dependencies]` | no | yes | no |

### Behavior

- `pantry install` (default/dev): installs `[dependencies]` + `[dev-dependencies]`
- `pantry install --env=test`: installs all three sections
- `pantry install --env=prod`: installs only `[dependencies]`

---

## 5. Build Output

Each environment produces output in a separate subdirectory:

```
build/
├── dev/
│   ├── output.ll
│   └── my-app
├── test/
│   ├── output.ll
│   └── my-app
└── prod/
    ├── output.ll
    └── my-app
```

This prevents dev builds from clobbering prod artifacts and vice versa.

---

## 6. Driver Integration

Pantry translates environment settings into compiler and linker flags:

```bash
# dev environment
saffronc --stdlib src/lib --lib-path .pantry/packages src/main.sf build/dev/output.ll
clang build/dev/output.ll -O0 -g -o build/dev/my-app -L/opt/homebrew/lib -lsaffron_rt

# prod environment
saffronc --stdlib src/lib --lib-path .pantry/packages src/main.sf build/prod/output.ll
clang build/prod/output.ll -O2 -flto --strip -o build/prod/my-app -L/opt/homebrew/lib -lsaffron_rt
```

### Flag mapping

| Environment field | Compiler/Linker flag |
|-------------------|---------------------|
| `optimization = N` | `-ON` passed to clang |
| `debug_info = true` | `-g` passed to clang |
| `strip = true` | `--strip` passed to linker |
| `lto = true` | `-flto` passed to clang |

---

## 7. Runtime Access: `@env` Module

A new stdlib module exposes the build environment at runtime:

```saffron
import "@env" as Env

// Query current environment
var current = Env.current()       // "dev", "test", or "prod"
var is_dev = Env.is_dev()         // true in dev builds
var is_prod = Env.is_prod()       // true in prod builds

// Access env vars defined in pantry.toml
var log_level = Env.get("LOG_LEVEL")   // "debug" in dev, "warn" in prod
```

### Implementation

The compiler injects environment information as string constants during codegen:

- `Env.current()` returns the environment name baked in at compile time
- `Env.get(key)` reads from `[environments.<name>.env-vars]` baked as a compile-time map, falling back to OS environment variables at runtime
- `Env.is_dev()` / `Env.is_prod()` / `Env.is_test()` are compile-time constants (enable dead-code elimination in prod)

---

## 8. Implementation

### Changes to existing files

| File | Change |
|------|--------|
| `pantry/src/config.sf` | Parse `[environments]`, `[dev-dependencies]`, `[test-dependencies]` sections |
| `pantry/src/commands/build.sf` | Accept `--env` flag, select environment, pass flags to driver |
| `pantry/src/commands/test.sf` | Default to `test` environment |
| `pantry/src/commands/install.sf` | Filter dependencies by environment scope |
| `tools/saffron` | Accept `--debug-info`, `--strip`, `--lto` flags; pass to clang |

### New files

| File | Purpose |
|------|---------|
| `src/lib/env.sf` | `@env` stdlib module |

### Implementation notes

- Environment selection is resolved early in the build command, before dependency resolution
- The `@env` module is compiled with string constants injected by the driver (similar to how `--stdlib` path is resolved)
- `pantry clean` removes all `build/` subdirectories
- Total implementation: ~100 lines in pantry, ~30 lines in `tools/saffron`, ~40 lines for `@env`

---

## 9. Open Questions

1. **Cross-compilation environments?** Could add `target = "wasm32"` to an environment. Defer to target-triple work.
2. **Environment inheritance?** e.g., `[environments.staging]` inherits from `prod` but overrides `env-vars`. Nice-to-have.
3. **Conditional compilation?** `@env.is_prod()` enables runtime branching, but compile-time `#[cfg(env = "prod")]` would allow dead code elimination. Future consideration.
4. **CI detection?** Auto-select `prod` when `CI=true` environment variable is set? Explicit is better.
