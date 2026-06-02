# Unused Variable Warnings — Implementation Design

## Overview

Add compile-time warnings for declared-but-never-read variables. Suppressed by `_` prefix convention.

## Data Structure Change

Add `used: Map<String, Bool>` to the `Scope` class in checker.sf:

```saffron
class Scope {
    var vars: Map<String, String>
    var narrowed: Map<String, String>
    var used: Map<String, Bool>       // NEW
}
```

## Three Touch Points

### 1. Mark on read (`infer_type`, Variable case)

```saffron
Variable(name) => {
    this.env.mark_used(name)
    this.parse_type_node(this.env.get_var(name))
}
```

### 2. Check before every `pop_scope()`

Call `this.check_unused_in_scope()` before each of the 7 scope-exit points:
- Function body (line 582)
- If-then/else branches (lines 599, 604)
- While loop body (line 610)
- Block statements (line 618)
- Try/catch/finally blocks (lines 633, 637, 640)
- Match arms (line 1273)

### 3. Suppress for `_` prefix

```saffron
fun check_unused_in_scope() {
    var scope: Scope = this.env.scopes[this.env.scopes.length() - 1]
    var declared: List<String> = scope.vars.keys()
    var i: Float = 0
    while (i < declared.length()) {
        var name: String = declared[i]
        if (!name.starts_with("_") and (!scope.used.has(name) or !scope.used.get(name))) {
            this.warn("unused variable '" + name + "'")
        }
        i = i + 1
    }
}
```

## Suppression Rules

| Name | Behavior |
|------|----------|
| `_` | True discard — no binding created |
| `_foo` | Binds but never warns if unused |
| `foo` | Warns if declared but never read |
| `__foo` | Internal (parser-generated temps) — never warns |

## Edge Cases

| Case | Decision |
|------|----------|
| Function params | Warn if unused (user can write `_unused: Type`) |
| Catch variable `e` | Warn (user can write `_e`) |
| For-loop temps (`__for_list`, `__for_i`) | Auto-suppressed (start with `__`) |
| Destructured bindings | Warn normally (user can use `_`) |
| Match arm bindings | Warn if not used in arm body |

## Complexity

**Medium** — ~2-3 hours. Changes localized to checker.sf only. No parser/codegen/runtime impact. Warnings don't block compilation.

## Files to Modify

- `src/compiler/checker.sf` — all changes here:
  - `Scope` class: add `used` map
  - `TypeEnv`: add `mark_used(name)` helper
  - `NullChecker`: add `check_unused_in_scope()` helper
  - `infer_type` Variable case: call `mark_used`
  - 7 scope-exit points: call `check_unused_in_scope()` before `pop_scope()`
