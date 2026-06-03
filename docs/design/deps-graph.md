# Dependency Graph Visualization

## Status: Proposal
## Date: 2026-06-03

---

## 1. Overview

`pantry deps` provides commands to inspect and visualize the dependency graph of a Saffron project. This helps developers understand their dependency tree, detect circular references, and generate diagrams for documentation.

---

## 2. CLI Interface

```
pantry deps              # alias for pantry deps.tree (ASCII tree)
pantry deps.tree         # ASCII tree output
pantry deps.graph        # DOT format output (for Graphviz)
```

### Options

| Flag | Description |
|------|-------------|
| `--depth N` | Limit recursion depth (default: unlimited) |
| `--imports` | Show file-level import graph instead of package graph |
| `<name>` | Filter to show subtree rooted at a specific package |

### Examples

```bash
pantry deps                     # full ASCII tree
pantry deps --depth 2           # tree limited to 2 levels
pantry deps.tree http           # subtree for 'http' package
pantry deps.graph > deps.dot    # DOT output for Graphviz
pantry deps.graph | dot -Tpng -o deps.png  # render to PNG
pantry deps --imports           # file-level import graph
```

---

## 3. ASCII Tree Output

### Format

```
$ pantry deps

my-app v0.1.0
├── turmeric v0.4.0 (path: ../turmeric)
│   ├── http v0.3.2 (git: github.com/saffron-lang/http)
│   │   └── json v1.0.1 (registry)
│   └── dom v0.1.0 (path: ../turmeric/packages/dom)
├── toml v0.2.3 (registry)
└── test v0.1.0 (path: src/lib/test.sf) [dev]
```

### Node format

Each node displays:
- Package name
- Version (from `pantry.lock` or `pantry.toml`)
- Source type: `(path: <relative>)`, `(git: <url>)`, or `(registry)`
- Scope marker: `[dev]` or `[test]` for non-production dependencies

### Box-drawing characters

```
├── branch with siblings below
└── last branch at this level
│   vertical continuation
```

### Circular dependency marker

```
my-app v0.1.0
├── plugin-a v1.0.0
│   └── plugin-b v2.0.0
│       └── plugin-a v1.0.0 (*circular*)
└── plugin-b v2.0.0
    └── plugin-a v1.0.0 (*circular*)
```

---

## 4. DOT Format Output

### Format

```dot
digraph dependencies {
    rankdir=LR;
    node [shape=box, style=rounded];

    "my-app v0.1.0";
    "turmeric v0.4.0";
    "http v0.3.2";
    "json v1.0.1";
    "toml v0.2.3";

    "my-app v0.1.0" -> "turmeric v0.4.0";
    "my-app v0.1.0" -> "toml v0.2.3";
    "turmeric v0.4.0" -> "http v0.3.2";
    "http v0.3.2" -> "json v1.0.1";
}
```

### Circular edges

Circular dependencies are rendered as dashed red edges:

```dot
    "plugin-a v1.0.0" -> "plugin-b v2.0.0";
    "plugin-b v2.0.0" -> "plugin-a v1.0.0" [style=dashed, color=red, label="circular"];
```

### Rendering

```bash
pantry deps.graph | dot -Tpng -o deps.png
pantry deps.graph | dot -Tsvg -o deps.svg
```

---

## 5. File-Level Import Graph (`--imports`)

When `--imports` is passed, the graph shows individual `.sf` files and their imports rather than packages:

```
$ pantry deps --imports

src/main.sf
├── src/parser.sf
│   ├── src/lexer.sf
│   └── src/ast.sf
│       └── src/types.sf
├── src/codegen.sf
│   ├── src/ast.sf (*seen*)
│   └── @iter (stdlib)
└── @test (stdlib)
```

This is useful for understanding the internal structure of a single package. The `(*seen*)` marker indicates a file that appears elsewhere in the tree (DAG, not repeated).

---

## 6. Depth Limiting

```
$ pantry deps --depth 1

my-app v0.1.0
├── turmeric v0.4.0 (path: ../turmeric) [+3 deps]
├── toml v0.2.3 (registry)
└── test v0.1.0 (path: src/lib/test.sf) [dev]
```

When depth is limited, nodes with unexpanded children show `[+N deps]` to indicate hidden subtree size.

---

## 7. Subtree Filtering

```
$ pantry deps.tree turmeric

turmeric v0.4.0 (path: ../turmeric)
├── http v0.3.2 (git: github.com/saffron-lang/http)
│   └── json v1.0.1 (registry)
└── dom v0.1.0 (path: ../turmeric/packages/dom)
```

Only the subtree rooted at the named package is displayed.

---

## 8. Resolution Algorithm

### Data sources

1. **`pantry.toml`** — declared dependencies with constraints
2. **`pantry.lock`** — resolved versions and sources
3. **Recursive dep `pantry.toml`s** — transitive dependencies read from resolved package directories

### Traversal

```
build_tree(pkg_name, visited):
    if pkg_name in visited:
        return CircularNode(pkg_name)
    visited.add(pkg_name)
    
    manifest = read_manifest(resolve_path(pkg_name))
    children = []
    for dep in manifest.dependencies:
        child = build_tree(dep.name, visited.copy())
        children.push(child)
    
    return TreeNode(pkg_name, manifest.version, children)
```

### Cycle detection

During traversal, a `visited` set tracks the current path from root. If a package appears in its own ancestor chain, it's marked `(*circular*)` and recursion stops.

---

## 9. Implementation

### New file: `pantry/src/commands/deps.sf`

```saffron
import "io" as IO
import "@fs" as FS
import "@toml" as TOML

class DepsCommand {
    var root_manifest: Manifest
    var lock: LockFile
    var format: String          // "tree" or "graph"
    var max_depth: Number
    var show_imports: Bool
    var filter_pkg: String?

    fun init(config: Config, args: List<String>) { ... }
    fun run() { ... }
    fun build_tree(name: String, visited: List<String>, depth: Number): TreeNode { ... }
    fun render_tree(node: TreeNode, prefix: String, is_last: Bool) { ... }
    fun render_dot(node: TreeNode): String { ... }
    fun resolve_one(name: String): ResolvedDep { ... }
}

class TreeNode {
    var name: String
    var version: String
    var source: String          // "path", "git", "registry"
    var source_detail: String   // path or URL
    var scope: String           // "main", "dev", "test"
    var children: List<TreeNode>
    var is_circular: Bool
}
```

### Extracted from resolver: `resolve_one()`

The `resolve_one()` function handles resolving a single dependency to its location on disk. This is extracted from `pantry/src/resolver.sf` so that the deps command can recursively resolve transitive dependencies without running full dependency resolution:

```saffron
fun resolve_one(name: String): ResolvedDep {
    // Check pantry.lock first (pinned resolution)
    if (this.lock.has(name)) {
        return this.lock.get(name)
    }
    // Fall back to pantry.toml constraint + search
    var constraint = this.root_manifest.dependencies.get(name)
    return resolve_from_constraint(name, constraint)
}
```

### Changes to existing files

| File | Change |
|------|--------|
| `pantry/src/main.sf` | Add `"deps"`, `"deps.tree"`, `"deps.graph"` to command dispatch |
| `pantry/src/resolver.sf` | Extract `resolve_one()` as a public method |

---

## 10. Open Questions

1. **Duplicate detection?** In a large tree, the same package may appear at multiple depths. Should we deduplicate (show only first occurrence, mark others as `(*seen*)`)? Tree format: no (show full). DOT format: yes (nodes are unique).
2. **Version conflict highlighting?** If the same package appears with different resolved versions (shouldn't happen with a lockfile but possible with path deps), highlight in red.
3. **Workspace awareness?** In a workspace, `pantry deps` from root shows the workspace member graph. `pantry deps --member <name>` shows that member's external deps.
4. **JSON output?** `pantry deps --format=json` for machine consumption. Straightforward addition.
