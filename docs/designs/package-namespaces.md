# Package Namespace Design

## Status: Proposal

## Problem

Saffron packages with multiple files (parsley, basil, turmeric) force consumers to know the internal file layout and import each sub-module separately:

```saffron
import "parsley/router" as Parsley
import "parsley/spec" as Spec
import "parsley/response" as Response
import "parsley/request" as Request
```

We want a single import to expose the full public API:

```saffron
import "parsley" as P

var api = P.router("/api/v1")
var resp = P.json(data)
var spec_json = P.to_json(api.spec())
```

Or with named imports:

```saffron
import { router, json, not_found, query_params, to_json } from "parsley"
```

---

## Current System Analysis

### Import Resolution (main.sf)

When the compiler encounters `import "parsley" as P`:

1. `resolve_import_path()` recognizes it as a bare import (no `./`, `../`, `@`)
2. `_find_in_lib_paths()` searches each `--lib-path` directory:
   - Checks `<lib_dir>/parsley.sf` (single file package)
   - Checks `<lib_dir>/parsley/pantry.toml` — reads `entry` field, resolves to `<lib_dir>/parsley/src/mod.sf`
   - Falls back to `<lib_dir>/parsley/src/lib.sf` (convention)

3. `collect_modules()` recursively processes the entry file's imports. When `mod.sf` contains `import "./router.sf" as Router`, it:
   - Resolves to the full path
   - Assigns a **path-based prefix** via `path_based_prefix()`: `"./router.sf"` becomes `router_`
   - Recursively collects that module's own imports

### Module Prefix System (codegen)

Each module gets a unique prefix prepended to all its function/class/global names in LLVM IR:

- `parsley/src/router.sf` with import path `./router.sf` (relative from mod.sf) gets prefix `router_`
- The codegen's `module_prefixes` map links user-facing aliases to these prefixes
- When user writes `P.router(...)`, codegen looks up `module_prefixes["P"]` to get the prefix

### The Fundamental Gap

When `bazaar/src/main.sf` does `import "parsley/router" as Parsley`:
- Import path is `parsley/router`
- Resolves to `<lib>/parsley/src/router.sf`
- Prefix becomes `parsley_router_` (from `path_based_prefix`)
- `Parsley.router(...)` resolves to `parsley_router_router` — works

But when doing `import "parsley" as P`:
- Resolves to `<lib>/parsley/src/mod.sf`
- mod.sf's prefix would be `src_mod_` or the entry file prefix
- `P.router(...)` looks for `<mod_prefix>router` — but `router()` is defined in router.sf with prefix `router_`
- **The alias P maps to mod.sf's prefix, not router.sf's prefix.** There is no mechanism to make `P.X` resolve to a function in a *different* module than the one P directly references.

### What mod.sf Currently Does

Today, `parsley/src/mod.sf` and `turmeric/src/index.sf` are barrel files that just import their sub-modules. They define no functions themselves. The `collect_modules` recursion ensures sub-module code is compiled, but the alias only maps to the entry file's namespace.

### How Turmeric Works Today

The bazaar frontend uses turmeric via sub-module imports:
```saffron
import { signal, computed, effect } from "turmeric/signal"
import { Event } from "turmeric/events"
import "turmeric/router" as Router
import { div, span, p, ... } from "turmeric/prelude"
```

The prelude auto-import mechanism (`src/prelude.sf`) is a separate system — it auto-includes prelude functions with empty prefix into any file that depends on turmeric. This works because the prelude defines functions directly; it's not a general re-export mechanism.

---

## Design Options

### Option A: Barrel Re-exports (export-from syntax)

```saffron
// parsley/src/mod.sf
export { router, ApiRouter, Endpoint } from "./router.sf"
export { json, not_found, bad_request, unauthorized, created } from "./response.sf"
export { parse_body, param, query_params, query_params_from_string } from "./request.sf"
export { to_json, to_manifest } from "./spec.sf"
```

**Semantics**: `export { X } from "path"` declares that name X (defined in the target module) is part of this module's public API. When someone imports this module (or uses named imports from it), they can access X.

**Implementation**:
- New parser production: `export { names } from "path"`
- Parser emits a new AST node (or a VarDecl with `@export:` docstring annotation)
- `collect_modules()` in main.sf reads export declarations from the entry file
- For each exported name, creates an alias mapping: `entry_prefix + name` -> `sub_module_prefix + name`
- Codegen adds these to `module_prefixes` or a new `export_aliases` map
- When resolving `P.router`, checks export aliases for the module P points to

**Pros**:
- Explicit control over public API surface
- Package author curates what's exported — internal helpers stay hidden
- Familiar pattern from TypeScript/ES modules
- Supports selective re-export (not everything)
- Named imports `import { X } from "parsley"` work naturally
- Self-documenting: mod.sf IS the public API definition

**Cons**:
- New keyword (`export`) must be added to lexer, parser, AST
- Bootstrap constraint: gen2 must be able to parse source that USES export (but compiler source won't use it)
- Package authors must maintain the export list manually
- More boilerplate in mod.sf

**Complexity**: Medium. New parser production + new resolution logic in collect_modules + new alias generation in codegen setup.

---

### Option B: Implicit Flattening (Go-style)

All public functions/classes from all `.sf` files in the package's `src/` directory automatically become part of the package namespace. No explicit re-export needed.

```saffron
// parsley/src/mod.sf — could be empty or just have doc comments
//! Parsley — Typed API framework for Saffron
```

Consumer writes `import "parsley" as P` and gets access to everything in all src/*.sf files.

**Implementation**:
- When `_find_in_lib_paths` resolves a package via pantry.toml, it also discovers all `.sf` files in the package
- A new field in the resolution result: `package_files: List<String>`
- `collect_modules()` processes all package files (not just what's imported from mod.sf)
- All package functions get a unified prefix: `parsley_`
- The alias `P` maps to this unified prefix

**Pros**:
- Zero boilerplate — just put files in src/ and they're exported
- Simple mental model: package = directory
- No new syntax needed
- Works today with minimal changes to collect_modules

**Cons**:
- No API boundary — every public function is exposed (internal helpers leak)
- Name collision risk: two files might define `parse()` or `init()`
- Changing file internals can accidentally break consumers
- No way to have package-private functions (everything public is exported)
- Harder to understand what a package's API is without reading all files
- If packages grow large, namespace pollution becomes a problem

**Complexity**: Low for basic version. But dealing with collisions and adding visibility modifiers later makes total complexity Medium-High.

---

### Option C: Qualified Sub-namespaces (Rust-style)

```saffron
import "parsley" as P
P.Router.router("/api/v1")
P.Response.json(data)
P.Spec.to_json(api.spec())
```

Sub-modules are accessible as nested namespaces under the package alias.

**Implementation**:
- When resolving `P.Router.router(...)`, codegen recognizes P as a package, Router as a sub-module within that package
- `module_prefixes["P"]` becomes a package marker (not a simple prefix)
- New resolution: `P.SubModule.func` -> look up sub-module prefix within the package
- Requires two-level lookup in codegen's MemberAccess handling

**Pros**:
- No new syntax in the language
- Clear disambiguation — no name collisions
- Easy to tell where a function comes from
- Package internals remain organized

**Cons**:
- Verbose: `P.Router.router(...)` is barely better than separate imports
- Doesn't solve the named-import problem: `import { router } from "parsley"` is ambiguous
- Two-level dot access requires deeper codegen changes (currently only one level of module prefix lookup)
- Doesn't match the desired UX in the problem statement

**Complexity**: Medium-High. Two-level MemberAccess resolution in codegen + package metadata tracking.

---

### Option D: Selective Flattening via mod.sf (import-as-star)

```saffron
// parsley/src/mod.sf
import "./router.sf" as *      // flatten all public names into this module
import "./response.sf" as *    // flatten all public names into this module
import { to_json, to_manifest } from "./spec.sf"  // selective
```

`import "path" as *` means "all public names from that module are treated as if defined in this module."

**Implementation**:
- New syntax: `import "path" as *` (glob import / re-export)
- Parser already handles `import "path" as <ident>` — extend to allow `*`
- In `collect_modules()`: when a module uses `as *`, all functions from the target get the IMPORTER's prefix instead of their own
- Essentially: router.sf's functions get compiled with mod.sf's prefix
- For selective: `import { X, Y } from "path"` in mod.sf already works for the current module — we just need to propagate those names to consumers

**Pros**:
- Minimal new syntax (just `as *`)
- Package author controls what's flattened vs selective
- Familiar glob import pattern
- Named imports from the package root work: names imported into mod.sf become part of its namespace

**Cons**:
- `as *` is a potential footgun (name collisions from multiple glob imports)
- Prefix assignment becomes complex: a function's prefix depends on whether it was glob-imported
- If router.sf defines `router()` and response.sf defines `response()`, they both need mod.sf's prefix — but router.sf might also be imported directly elsewhere with its own prefix. One function gets two prefixes?
- Requires resolving the "dual prefix" problem or making glob import create duplicates

**Complexity**: Medium. The key difficulty is that a single function definition might need to be accessible under multiple prefixes (its native prefix when imported directly, and the entry file's prefix when accessed through the package root).

---

## Recommendation: Option A (Barrel Re-exports) with a twist

**Recommended approach: Option A**, implemented as a lightweight annotation rather than a full `export` keyword to minimize bootstrap impact.

### Why Option A

1. **Explicit > Implicit**: Package authors define their public API surface. Internal helpers (functions starting with `_`, utility functions) stay private. This is the #1 reason — as packages grow, you need an API boundary.

2. **Named imports work naturally**: `import { router, json } from "parsley"` pulls from the set of exported names, which is well-defined.

3. **Backwards compatible**: Existing code that imports sub-modules directly (`import "parsley/router" as Router`) continues to work unchanged. The new system adds a capability without removing the old one.

4. **Solves the turmeric case**: `import { signal, computed, div, Link } from "turmeric"` would pull from turmeric's index.sf exports, even though those functions are defined in signal.sf, prelude.sf, and router.sf respectively.

5. **Self-documenting**: Reading mod.sf tells you the package's complete public API.

### Implementation: Annotation-based (avoids new keyword)

Instead of adding a new `export` keyword (which requires lexer + parser changes that affect gen2), use the existing doc-comment annotation pattern:

```saffron
// parsley/src/mod.sf
//! Parsley — Typed API framework for Saffron

/// @export-from: ./router.sf
/// router, ApiRouter, Endpoint

/// @export-from: ./response.sf
/// json, not_found, bad_request, unauthorized, created

/// @export-from: ./request.sf
/// parse_body, param, query_params, query_params_from_string

/// @export-from: ./spec.sf
/// to_json, to_manifest
```

Wait — this doesn't parse into AST nodes. Better approach: use the existing import + a new marker that the compiler recognizes:

```saffron
// parsley/src/mod.sf
//! Parsley — Typed API framework for Saffron

import "./router.sf" as Router       // @public
import "./response.sf" as Response   // @public
import "./request.sf" as Request     // @public
import "./spec.sf" as Spec           // @public: to_json, to_manifest
```

Actually, the cleanest approach given Saffron's conventions is to use the **existing `import { ... } from` syntax** in mod.sf combined with a new resolution rule:

### Final Design: "Entry File Exports"

**Rule**: When a consumer imports a package root (`import "parsley" as P` or `import { X } from "parsley"`), the compiler resolves to the entry file (mod.sf). Any name that mod.sf makes available via `import { X } from "./sub.sf"` is treated as part of the package's exported namespace.

The entry file (mod.sf) uses standard named imports to declare its public API:

```saffron
// parsley/src/mod.sf
//! Parsley — Typed API framework for Saffron

import { router, ApiRouter, Endpoint } from "./router.sf"
import { json, not_found, bad_request, unauthorized, created } from "./response.sf"
import { parse_body, param, query_params, query_params_from_string } from "./request.sf"
import { to_json, to_manifest } from "./spec.sf"
```

**Consumer usage**:
```saffron
import "parsley" as P
P.router("/api/v1")    // resolves to router_router (the actual function)
P.json(data)           // resolves to response_json

import { router, json, to_json } from "parsley"
router("/api/v1")      // works
json(data)             // works
```

**How it works in the compiler**:

1. `collect_modules()` processes the entry file (mod.sf)
2. Detects named imports in the entry file: `import { X, Y } from "./sub.sf"`
3. For each named import X from sub-module S:
   - Resolves S's prefix (e.g., `router_` for `./router.sf`)
   - Records: `package_exports[X] = sub_module_prefix + X`
4. When the consumer writes `import "parsley" as P`:
   - `module_prefixes["P"]` gets a special package prefix (e.g., `__pkg_parsley_`)
   - But additionally, each export alias is registered: `P.router` -> `router_router`, `P.json` -> `response_json`
5. In codegen, when resolving `P.router(...)`:
   - First check package export aliases
   - If found, use the actual prefixed function name

For `import { router, json } from "parsley"`:
- Process the entry file to determine available exports
- Each named import maps directly to the sub-module's prefixed name
- `named_imports["router"] = "router_router"` (same as if they'd written `import { router } from "parsley/router"`)

---

## Detailed Design for Parsley

### parsley/src/mod.sf (new)
```saffron
//! Parsley — Typed API framework for Saffron
//!
//! Usage:
//!   import "parsley" as P
//!   var api = P.router("/api/v1")
//!   api.get("/packages", list_packages)
//!   api.mount(app)

import { router, ApiRouter, Endpoint } from "./router.sf"
import { json, not_found, bad_request, unauthorized, created } from "./response.sf"
import { parse_body, param, query_params, query_params_from_string } from "./request.sf"
import { to_json, to_manifest } from "./spec.sf"
```

### Consumer code (bazaar/src/main.sf after migration)
```saffron
import "parsley" as P
import "./routes.sf" as Routes

fun main() {
    var app = Http.server(3000)
    var api = P.router("/api/v1")
    Routes.register(api)
    api.mount(app)

    var spec_json = P.to_json(api.spec())
    IO.write_file("static/api_spec.json", spec_json)
    app.serve()
}
```

### Consumer code (bazaar/src/routes.sf after migration)
```saffron
import { ApiRouter } from "parsley"
import { json, not_found, unauthorized, bad_request, created } from "parsley"
import { param, query_params, parse_body } from "parsley"
```

Or combined:
```saffron
import { ApiRouter, json, not_found, param, query_params, parse_body } from "parsley"
```

---

## Detailed Design for Turmeric

### turmeric/src/index.sf (new)
```saffron
//! Turmeric — Reactive Web Framework for Saffron

import { signal, computed, effect, batch } from "./signal.sf"
import { Event } from "./events.sf"
import { HashRouter, Link, RouteParams } from "./router.sf"
import { style, cx, keyframes } from "./style.sf"
import { createContext, useContext } from "./context.sf"
import { Suspense, lazy } from "./suspense.sf"
import { div, span, p, a, h1, h2, nav, form, input, button, ... } from "./prelude.sf"
```

### Consumer (bazaar frontend after migration)
```saffron
import { signal, computed, effect } from "turmeric"
import { Event } from "turmeric"
import { HashRouter, Link, RouteParams } from "turmeric"
import { div, span, p, a, h1, h2, nav, form, input, button, mount } from "turmeric"
```

Or with qualified access:
```saffron
import "turmeric" as T
var count = T.signal(0)
T.effect(fun () => IO.println(count.get().to_string()))
```

---

## Detailed Design for Basil

### basil/src/mod.sf (new)
```saffron
//! Basil — Auto-caching query client for Turmeric frontends

import { Query, query, query_with } from "./query.sf"
import { Mutation, mutation } from "./mutation.sf"
import { invalidate_all, clear_cache } from "./cache.sf"
```

### Consumer
```saffron
import "basil" as B
var packages = B.query("/api/v1/packages")

// Or:
import { query, mutation, Query, Mutation } from "basil"
```

---

## Compiler Changes Required

### 1. main.sf — Entry File Export Extraction

New function: `extract_package_exports()`

```
Location: src/compiler/main.sf (after line ~267, near extract_named_imports)
```

When the compiler resolves a bare package import to an entry file:
1. Read the entry file
2. Extract its named imports (`import { X, Y } from "./sub.sf"`)
3. For each name, resolve the sub-module path and compute its prefix
4. Return a map: `export_name -> prefixed_name`

Changes to `collect_modules()`:
- After resolving a package entry file, call `extract_package_exports()`
- Store results in a new `package_exports: Map<String, String>` parameter

Changes to the main compilation flow (line ~628+):
- When processing `import "parsley" as P`:
  - Detect that this is a package root import (resolved via pantry.toml)
  - Extract exports from the entry file
  - For each export name, add to `resolved_aliases` or a new package-aware map
- When processing `import { X, Y } from "parsley"`:
  - Resolve to entry file
  - Look up X, Y in the entry file's exports
  - Map each to its actual prefixed function name in `named_imports`

### 2. main.sf — Module Prefix for Package Alias

When user writes `import "parsley" as P`, the `module_prefixes["P"]` needs special handling. Currently it would map to the entry file's prefix. Instead:

Option A (simple): Don't register P in module_prefixes at all. Instead, register each export individually:
- `module_prefixes["P"]` is not set
- Instead, a new `package_alias_exports: Map<String, Map<String, String>>` maps:
  - `"P" -> { "router": "router_router", "json": "response_json", ... }`

Option B (simpler, works with existing codegen): Generate a synthetic prefix for the package and register alias functions:
- Give the package a prefix: `parsley_`
- For each export, create an LLVM alias or forwarding entry
- But this requires LLVM-level changes

**Recommended**: Option A (new lookup table). The change is localized to the MemberAccess resolution in expr_body.sf.

### 3. codegen/expr_body.sf — Package-Aware MemberAccess

In the MemberAccess handling (around line 208, 1273), add a check:

```
// After checking module_prefixes:
if (package_exports.has(obj_name)) {
    var pkg_exports = package_exports.get(obj_name)
    if (pkg_exports.has(field)) {
        var resolved = pkg_exports.get(field)
        // resolved is the full prefixed function name
        return call_or_ref(resolved)
    }
}
```

### 4. codegen/expr_body.sf — Package-Aware Call Resolution

In the call resolution (around line 1581-1601), add package export lookup:

```
// When resolving Alias.func() calls:
if (package_exports.has(alias)) {
    var resolved = package_exports.get(alias).get(func_name)
    // Use resolved as the callee
}
```

### 5. No Parser Changes Required

The entry file uses existing `import { X } from "path"` syntax. No new keywords or grammar needed. This is critical for bootstrap compatibility.

### 6. No AST Changes Required

Everything is handled in the compilation pipeline (main.sf resolution + codegen symbol lookup). The AST remains unchanged.

---

## File-by-File Change Summary

| File | Change | Complexity |
|------|--------|-----------|
| `src/compiler/main.sf` | Add `extract_package_exports()`, modify `collect_modules()` call site to detect package imports and build export maps | Medium |
| `src/compiler/codegen.sf` | Add `package_exports: Map<String, Map<String, String>>` field to Codegen class, thread it through `generate_with_modules_flat_opts3` | Low |
| `src/compiler/codegen/expr_body.sf` | Add package export lookup in MemberAccess and Call resolution | Medium |
| `parsley/src/mod.sf` | Rewrite to use named imports (defines public API) | Low |
| `turmeric/src/index.sf` | Rewrite to use named imports (defines public API) | Low |
| `basil/src/mod.sf` | Rewrite to use named imports (defines public API) | Low |

---

## Backwards Compatibility

1. **Existing sub-module imports still work**: `import "parsley/router" as Router` resolves via `_find_submodule_in_lib_paths` and is unaffected.

2. **Existing named imports from sub-modules still work**: `import { json } from "parsley/response"` continues to resolve directly.

3. **Package root imports gain new power**: `import "parsley" as P` currently compiles but `P.router()` would fail to resolve. After this change, it works. This is purely additive.

4. **No syntax changes**: All imports use existing syntax. No new keywords.

5. **The entry file's named imports are the API declaration**: If mod.sf doesn't use named imports from its sub-modules, the package exports nothing through the root — same as today.

---

## Named Import Resolution Detail

When user writes:
```saffron
import { router, json, to_json } from "parsley"
```

Current flow:
1. `extract_named_imports()` extracts `{"parsley": ["router", "json", "to_json"]}`
2. `resolve_import_path("parsley")` -> `<lib>/parsley/src/mod.sf`
3. Prefix for mod.sf is computed (e.g., `src_mod_`)
4. `named_imports["router"] = "src_mod_router"` — **WRONG** (router isn't defined in mod.sf)

New flow:
1. Same extraction
2. Same resolution to entry file
3. **NEW**: Detect this is a package entry file (was resolved via pantry.toml)
4. **NEW**: Parse entry file's named imports to build export map
5. For "router": entry file has `import { router } from "./router.sf"` → router.sf prefix is `router_`
6. `named_imports["router"] = "router_router"` — **CORRECT**
7. For "json": entry file has `import { json } from "./response.sf"` → response.sf prefix is `response_`
8. `named_imports["json"] = "response_json"` — **CORRECT**

The key insight: **the entry file's named imports define a name -> sub-module mapping that the compiler follows to find the actual prefixed symbol.**

---

## Detection: "Is This a Package Entry File?"

The compiler needs to distinguish between:
- `import "parsley" as P` (package root → entry file via pantry.toml)
- `import "./routes.sf" as Routes` (direct file import)

The information is already available: `_find_in_lib_paths()` resolves via pantry.toml. We just need to propagate a flag or store which resolved paths came from package resolution.

Simplest approach: when `resolve_import_path()` resolves a bare import via pantry.toml, record it:
```
_package_entry_files.set(resolved_path, pkg_name)
```

Then when building named_imports, check if the target path is in `_package_entry_files`.

---

## Edge Cases

### Re-exporting classes with methods

When parsley exports `ApiRouter` (a class from router.sf), the consumer needs:
- Constructor: `P.ApiRouter(prefix)` or via `var api = P.router("/api/v1")` which returns an ApiRouter
- Methods: `api.get(...)`, `api.mount(...)` — these work via instance dispatch, no prefix needed

Class re-export works because class registration in codegen uses the class name itself (with prefix) for struct layout. The `ApiRouter` class from router.sf gets registered as `router_ApiRouter` in `class_fields`/`class_methods`. The export alias just needs to map constructor calls.

### Name collisions between sub-modules

If router.sf and response.sf both define a function named `parse()`:
- Entry file must choose which to export: `import { parse } from "./router.sf"`
- Or rename: not supported yet (future: `import { parse as parse_route } from "./router.sf"`)

For now, name collisions are resolved by the package author choosing what to export. A future enhancement could add rename-on-import syntax.

### Circular exports

Entry file imports from sub-modules. Sub-modules might import from each other. This is already handled by `collect_modules()`'s visited set. The export extraction just reads the entry file's import declarations — it doesn't trigger additional compilation.

### Deep re-exports

If sub-module A imports from sub-module B and the entry file exports from A, does that include B's symbols? No — only the names explicitly listed in the entry file's named imports are exported. The entry file is the single source of truth.

---

## Implementation Order

1. **Phase 1**: Add package export extraction to main.sf (parse entry file's named imports, build export map)
2. **Phase 2**: Thread export map through to codegen, add package-aware resolution in expr_body.sf
3. **Phase 3**: Update parsley/basil/turmeric mod.sf/index.sf files
4. **Phase 4**: Update bazaar to use simplified imports (optional, backwards compat)

Phase 1 and 2 are the core work. Phase 3 is just editing package entry files. Phase 4 is optional cleanup.

---

## Future Enhancements (out of scope)

- `export` keyword for symmetry with other languages (syntactic sugar for the named-import pattern)
- Rename on re-export: `import { parse as route_parse } from "./router.sf"`
- Wildcard re-export: `import * from "./router.sf"` (export everything from sub-module)
- Visibility modifiers: `pub fun`, `pub class` to mark what's importable
- `pantry.toml` exports field: `exports = ["router", "json", ...]` (alternative to code-based declaration)
