# Module Resolution Design

## Overview

`saffronc` resolves imports by searching a list of paths in order. This enables the stdlib, third-party packages, and local shared libraries to coexist cleanly.

## CLI Interface

```
saffronc --stdlib <path> [--lib-path <path>]... <input.sf> <output.ll>
```

- `--stdlib <path>` — the standard library root (required, typically `src/lib`)
- `--lib-path <path>` — additional search paths (repeatable, searched in order)

Example:

```bash
saffronc --stdlib src/lib \
         --lib-path .pantry/packages \
         --lib-path ../shared-libs \
         src/main.sf output.ll
```

## Import Syntax & Resolution

### Relative imports

```saffron
import "./config.sf" as Config
import "../utils/helpers.sf" as Helpers
```

Resolved relative to the **importing file's directory**. No search path involved.

### `@` imports (packages/stdlib)

```saffron
import "@toml" as TOML
import "@http" as Http
import "@mypackage" as MyPkg
```

Searched in order through:
1. `--stdlib` path
2. Each `--lib-path` in the order specified

For each search path, the resolver looks for:
1. `<search_path>/<name>.sf` — single-file module
2. `<search_path>/<name>/` directory with a `pantry.toml` → uses `entry` field
3. `<search_path>/<name>/src/lib.sf` — convention fallback if no pantry.toml

First match wins.

### Bare imports (package name, no prefix)

```saffron
import "turmeric" as UI
```

Same resolution as `@` imports — searched through lib paths. The `@` prefix is optional sugar that makes it explicit the import is a package rather than a relative file.

## Pantry Integration

### Install step

`pantry install` resolves dependencies and prepares them for the compiler:

```
.pantry/
└── packages/
    ├── turmeric/          → symlink or copy of resolved source
    │   ├── pantry.toml
    │   └── src/
    │       └── lib.sf     ← entry point
    └── http-client/
        ├── pantry.toml
        └── src/
            └── lib.sf
```

For **path deps**: symlink to the source directory
For **git deps**: the clone in `.pantry/git/<name>` IS the package dir (or symlink from packages/)

### Build step

Pantry's build command invokes:

```bash
saffronc --stdlib src/lib --lib-path .pantry/packages src/main.sf output.ll
```

The compiler then resolves `import "@turmeric"` by finding `.pantry/packages/turmeric/`, reading its `pantry.toml` for `entry = "src/lib.sf"`, and bundling that file.

## Resolution Algorithm (pseudocode)

```
resolve(import_name, importing_file, search_paths):
    if import_name starts with "./" or "../":
        return resolve_relative(import_name, dirname(importing_file))
    
    name = strip_prefix(import_name, "@")  // "@toml" → "toml"
    
    for path in search_paths:
        // Try single file
        if exists(path / name + ".sf"):
            return path / name + ".sf"
        
        // Try directory with pantry.toml
        if exists(path / name / "pantry.toml"):
            manifest = parse_toml(path / name / "pantry.toml")
            return path / name / manifest.entry
        
        // Try convention: dir/src/lib.sf
        if exists(path / name / "src/lib.sf"):
            return path / name / "src/lib.sf"
    
    error("module not found: " + import_name)
```

## Future Considerations

- **Versioned paths**: `.pantry/packages/toml@1.2.0/` for multiple versions
- **Lockfile-driven resolution**: `pantry.lock` pins exact paths so builds are reproducible
- **Virtual packages**: `@std` as an alias for the entire stdlib namespace
- **Conditional imports**: platform-specific module resolution (future)
