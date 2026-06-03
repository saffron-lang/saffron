# Pantry Workspaces (Monorepo Support)

## Status: Proposal
## Date: 2026-06-03

---

## 1. Overview

Workspaces allow multiple Saffron packages to live in a single repository with shared dependency resolution, a single lockfile, and coordinated builds. This mirrors the monorepo patterns found in Cargo workspaces, npm workspaces, and Go modules.

---

## 2. Configuration

### Root `pantry.toml`

```toml
[workspace]
members = [
    "pantry",
    "turmeric",
    "examples/*",
]
exclude = [
    "examples/experimental-*",
]

[workspace.dependencies]
http = "^0.3.0"
json = "^1.0.0"
toml = "^0.2.0"
```

- `members` — list of paths (relative to root) or globs identifying workspace packages
- `exclude` — optional glob patterns to filter out matched paths
- `[workspace.dependencies]` — shared dependency versions referenced by members

### Virtual Workspaces

A root `pantry.toml` with only `[workspace]` and no `[package]` section is a virtual workspace — it is not itself a package, only an orchestrator:

```toml
[workspace]
members = ["packages/*"]
```

### Member `pantry.toml`

Members reference workspace-level dependency versions with `{ workspace = true }`:

```toml
[package]
name = "turmeric"
version = "0.4.0"

[dependencies]
http = { workspace = true }
json = { workspace = true }
```

This resolves to the version constraint declared in `[workspace.dependencies]` at the root.

---

## 3. Dependency Resolution

### Hoisted packages

All resolved dependencies live in a single `.pantry/packages/` directory at the workspace root:

```
my-workspace/
├── pantry.toml              ← workspace root
├── pantry.lock              ← single lockfile
├── .pantry/
│   └── packages/
│       ├── http/
│       ├── json/
│       └── toml/
├── pantry/
│   ├── pantry.toml
│   └── src/
├── turmeric/
│   ├── pantry.toml
│   └── src/
└── examples/
    ├── hello/
    │   ├── pantry.toml
    │   └── src/
    └── web-app/
        ├── pantry.toml
        └── src/
```

### Inter-member dependencies

Workspace members can depend on each other without explicit path declarations. When member A imports `@member-b`, the resolver checks workspace members before searching `.pantry/packages/`:

```saffron
// In examples/web-app/src/main.sf
import "@turmeric" as UI
```

This resolves because `turmeric` is a workspace member. The workspace root is added as a `--lib-path` during compilation so the compiler finds member directories directly.

### Single lockfile

One `pantry.lock` at the workspace root pins all resolved versions across all members. This guarantees all members use the same version of shared dependencies.

---

## 4. Build Orchestration

### Topological ordering

`pantry build` from the workspace root builds all members in dependency order. Given:

```
turmeric → depends on nothing
pantry → depends on nothing
examples/web-app → depends on turmeric
```

Build order: `turmeric`, `pantry`, then `examples/web-app` (or parallel where no dependency exists).

### Cycle detection

The workspace resolver performs a topological sort on inter-member dependencies. If a cycle is detected:

```
error: cyclic dependency between workspace members
  → pantry depends on turmeric
  → turmeric depends on pantry
```

### Command propagation

| Command | Behavior from workspace root |
|---------|------------------------------|
| `pantry build` | Build all members (topological order) |
| `pantry test` | Run tests for all members |
| `pantry install` | Resolve all deps into shared `.pantry/packages/` |
| `pantry clean` | Remove workspace-level `.pantry/` and all build artifacts |

### Targeting a specific member

```bash
pantry build --member turmeric
pantry test --member pantry
```

The `--member <name>` flag restricts the command to a single member (and its transitive workspace deps if needed for the build).

---

## 5. Compiler Integration

When building a workspace member, pantry invokes the compiler with:

```bash
saffronc --stdlib src/lib \
         --lib-path /path/to/workspace-root \
         --lib-path /path/to/workspace-root/.pantry/packages \
         member/src/main.sf output.ll
```

The workspace root as a `--lib-path` means `import "@member-name"` resolves by finding the member directory and reading its `pantry.toml` entry point — following standard module resolution rules.

---

## 6. Implementation

### New file: `pantry/src/workspace.sf`

```saffron
class Workspace {
    var root: String
    var members: List<String>
    var exclude: List<String>
    var shared_deps: Map<String, String>

    fun init(root: String) { ... }
    fun discover_members(): List<Package> { ... }
    fun resolve_order(): List<Package> { ... }
    fun is_member(name: String): Bool { ... }
}
```

Key functions:
- `discover_members()` — expand globs, filter excludes, parse each member's `pantry.toml`
- `resolve_order()` — topological sort with cycle detection
- `is_member(name)` — check if an import target is a workspace member (for lib-path resolution)

### Changes to existing files

| File | Change |
|------|--------|
| `pantry/src/config.sf` | Parse `[workspace]` section, `{ workspace = true }` dep references |
| `pantry/src/commands/build.sf` | Iterate members in topological order when workspace detected |
| `pantry/src/commands/install.sf` | Merge all member deps, resolve once, install to root `.pantry/packages/` |
| `pantry/src/commands/test.sf` | Propagate to all members |
| `pantry/src/resolver.sf` | Accept workspace deps map, resolve `{ workspace = true }` references |

---

## 7. Example: Saffron Repo as a Workspace

```toml
# /saffron/pantry.toml
[workspace]
members = [
    "pantry",
    "turmeric",
    "examples/*",
]

[workspace.dependencies]
test = { path = "src/lib/test.sf" }
```

```
saffron/
├── pantry.toml
├── pantry.lock
├── .pantry/packages/
├── pantry/
│   ├── pantry.toml
│   └── src/
├── turmeric/
│   ├── pantry.toml
│   └── src/
└── examples/
    ├── hello/
    │   ├── pantry.toml
    │   └── src/main.sf
    └── web-app/
        ├── pantry.toml
        └── src/main.sf
```

---

## 8. Open Questions

1. **Version conflicts?** If member A wants `http ^0.3.0` and member B wants `http ^0.4.0`, workspace resolution should error (single hoisted copy). Members must agree on major version.
2. **Publishing from workspace?** `pantry publish --member turmeric` should package only that member with its resolved deps.
3. **Nested workspaces?** Not supported initially. A workspace member cannot itself be a workspace root.
4. **Dev dependencies per-member vs workspace-level?** Both: members can have their own `[dev-dependencies]`, workspace can have `[workspace.dev-dependencies]`.
