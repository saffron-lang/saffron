# Receiver Closures (Implicit `this` in Trailing Closures)

## Status: Proposal
## Date: 2026-06-03

---

## Summary

Enable Kotlin-style receiver closures where the closure body can access fields/methods of a receiver type without qualification:

```saffron
Pantry.project {
    name = "hello"          // this.name = "hello"
    version = "0.1.0"      // this.version = "0.1.0"
    dep("http", "^2.0.0")  // this.dep(...)
}
```

---

## 1. Syntax: `Type.() => ReturnType`

```saffron
fun project(config: Project.() => Nil)
fun request(config: HttpRequest.() => Nil): HttpRequest
fun div(block: DivScope.() => Nil): Node
```

Reads as "a closure whose `this` is a `Project`." The dot notation mirrors existing `Type.method()` member access.

Encoding in the type system: `"Fun[Project]():Nil"` — receiver type in brackets before the param list.

---

## 2. Name Resolution Order

Inside a receiver closure:

1. **Local variables** declared within the closure
2. **Closure parameters** (if any)
3. **Receiver's fields and methods** (implicit `this`)
4. **Enclosing scope** (captured variables, module-level names)

Locals always win. `this.name` explicitly accesses the receiver when disambiguation is needed.

---

## 3. Nesting

Innermost receiver wins:

```saffron
Pantry.project {
    // receiver = Project
    name = "hello"

    script("build") {
        // receiver = Script
        run("test")         // Script.run(), not Project.run()
    }
}
```

To access an outer receiver, capture it first:

```saffron
Pantry.project {
    var proj = this
    script("build") {
        run("test")
        // proj.name still accessible via captured variable
    }
}
```

Future: `this@Project` qualified access (Kotlin-style). Not needed for v1.

---

## 4. Type Inference

The compiler infers the receiver type from the function signature:

```saffron
fun project(config: Project.() => Nil) { ... }

// At call site — compiler knows receiver = Project from param type
Pantry.project {
    name = "hello"   // checker resolves as Project.name
}
```

The parser produces a normal Lambda node. The checker adds receiver semantics based on the expected type at the call site.

---

## 5. Parser Changes (Minimal)

Only type annotation parsing needs updating. In `parse_single_type()`:

- After parsing identifier `Name`, if followed by `.` then `(`:
  - If the paren section is followed by `=>`, parse as receiver function type
  - Otherwise, continue as dotted type name

Lambda AST nodes do NOT change. The receiver is resolved during type checking.

---

## 6. Type Checker Changes

1. **Receiver function type recognition**: Parse `"Fun[Project]():Nil"` to extract receiver type
2. **Scope enrichment**: When a Lambda is used where a receiver type is expected, push the receiver type's fields/methods into scope
3. **Bare name resolution**: If a `Variable(name)` isn't local, check receiver fields/methods
4. **AST desugaring** (preferred approach): Transform the Lambda body:
   - `Variable("name")` where `name` is a receiver field → `GetField(This, "name")`
   - `Assign("name", value)` where `name` is a receiver field → `SetField(This, "name", value)`
   - `Call(Variable("dep"), args)` where `dep` is a receiver method → `MethodCall(This, "dep", args)`
   - Prepend a hidden `self` parameter to the Lambda

---

## 7. Codegen Changes

Using the desugaring approach: **zero codegen changes needed**.

The desugared Lambda already uses `This`, `SetField`, `MethodCall` — all of which the codegen handles. The hidden `self` parameter compiles exactly like method `this`.

At the call site: pass the receiver instance as the first argument to the closure.

---

## 8. Use Cases

### Pantry DSL

```saffron
Pantry.project {
    name = "my-app"
    version = "1.0.0"
    dep("http", "^2.0")

    script("build") {
        run("saffron build src/main.sf")
    }
}
```

### Builder pattern

```saffron
var req = Http.request {
    url = "https://api.example.com"
    method = "POST"
    header("Content-Type", "application/json")
    body = JSON.stringify(payload)
}
```

### UI framework (turmeric)

```saffron
fun div(block: DivScope.() => Nil): Node

div {
    cls("container")
    style("padding: 1rem")

    h1 { text("Hello") }

    button {
        text("Click me")
        on_click { count.update(fun (n) => n + 1) }
    }
}
```

### Test DSL

```saffron
Test.describe("math") {
    test("addition") {
        assert_eq(1 + 1, 2)
    }

    test("multiplication") {
        assert_eq(3 * 4, 12)
    }
}
```

---

## 9. Interaction with Trailing Closures

No change to trailing closure parsing. The difference is purely semantic:

- `() => Nil` parameter → normal closure (no implicit this)
- `Project.() => Nil` parameter → receiver closure (this = Project instance)

The parser produces identical Lambda nodes. The checker distinguishes based on expected type.

---

## 10. Turmeric Migration

Current (context stack approach):
```saffron
fun div(attrs: List<Attr> = [], block: () => Nil = nil): Node {
    var el = _tc_create("div")
    if (block != nil) { _tc_push(el); block(); _tc_pop() }
    return el
}
```

With receiver closures (optional, incremental):
```saffron
class DivScope {
    var el: Node
    fun text(content: String) { ... }
    fun cls(name: String) { ... }
    fun on_click(handler: () => Nil) { ... }
}

fun div(block: DivScope.() => Nil): Node {
    var scope = DivScope()
    scope.el = create_element("div")
    block(scope)
    return scope.el
}
```

Migration is optional — existing `() => Nil` approach continues to work.

---

## Implementation Phases

1. **Phase 1 — Type syntax** (parser): Parse `Type.() => Ret`, encode as `"Fun[Type]():Ret"`
2. **Phase 2 — AST desugaring** (checker): Transform bare names to explicit receiver access
3. **Phase 3 — Call site** (codegen): Pass receiver as first arg to receiver closures
4. **Phase 4 — Inference** (checker): Propagate receiver type from call-site param types

Estimated effort: ~300 lines across parser + checker + codegen.

---

## Critical Files

- `src/compiler/parser.sf` — type annotation parsing
- `src/compiler/checker.sf` — scope resolution, desugaring pass
- `src/compiler/ast.sf` — no changes needed (uses existing nodes)
- `src/compiler/codegen/closures_body.sf` — call-site receiver passing
