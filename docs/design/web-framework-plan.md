# Saffron Web Framework Plan

A full-featured SPA framework targeting WASM, with fine-grained reactivity and a Compose/SwiftUI-style builder API.

## Design Principles

- **Signals over VDOM** — fine-grained reactivity avoids expensive WASM↔JS boundary crossings for diffing
- **Builder syntax** — `tag(attrs) { children }` via trailing closures, not JSX
- **Compile-time analysis** — the compiler statically tracks signal dependencies and emits surgical DOM update code
- **Minimal JS glue** — a small (~2KB) JS runtime manages a DOM handle table; everything else runs in WASM

## Phase Plan

### Phase 0: WASM Target from LLVM Backend

Extend the existing LLVM backend to target `wasm32-unknown-unknown`.

- GC compiles into the WASM binary (operates on linear memory)
- Tree-shake with LTO + `--gc-sections` to minimize binary size
- Target: 300KB–1MB binary including runtime + stdlib

### Phase 1: JS Interop Bridge

The critical foundation. WASM cannot touch the DOM directly — every DOM operation crosses the WASM↔JS boundary via imported functions.

```saffron
@extern("js")
fun createElement(tag: String): DomHandle

@extern("js")
fun setAttribute(el: DomHandle, key: String, value: String)

@extern("js")
fun addEventListener(el: DomHandle, event: String, callback: Fun(): Void)
```

Key pieces:
- `DomHandle` — integer index into a JS-side table of live DOM references (same pattern as wasm-bindgen)
- String marshalling — WASM linear memory uses UTF-8, JS uses UTF-16, need a conversion layer
- Callback routing — JS captures events, looks up registered WASM callback handle, calls into WASM

### Phase 2: Trailing Closure Syntax

Add trailing-block syntax to the parser:

```
call_expr → expr "(" args ")" block?
```

This makes `div(class: "x") { ... }` parse as `div(class: "x", children: fun () => { ... })`.

No runtime changes needed — purely a parse-time desugaring.

### Phase 3: Signal/Effect/Computed Primitives

Core reactivity as a Saffron stdlib (`@ui` or similar):

```saffron
import "@ui" as UI

var count = UI.signal(0)
var doubled = UI.computed(fun () => count.get() * 2)

UI.effect(fun () => {
    IO.println("count changed: ${count.get()}")
})
```

These are testable without any DOM — pure reactive graph.

### Phase 4: Component Model + Compile-Time Lowering

Element functions with trailing blocks define the component tree:

```saffron
fun Counter(initial: Int): UI.Element {
    var count = UI.signal(initial)

    div(class: "counter") {
        h1 { "Count: ${count}" }
        button(on_click: fun () => count.set(count.get() + 1)) {
            "Increment"
        }
    }
}
```

The compiler lowers templates to direct DOM creation + effect wiring:

```
// Compiled output (conceptual):
var el = createElement("h1")
var text = createTextNode("")
appendChild(el, text)
effect(fun () => setTextContent(text, "Count: " + count.get().to_string()))
```

No VDOM. No diffing. Surgical updates per reactive dependency.

### Phase 5: Event Dispatch (JS → WASM)

Events cross JS→WASM:

```javascript
// JS shim (generated)
element.addEventListener('click', (e) => {
    wasm.exports.__dispatch_event(callbackId, serializeEvent(e))
})
```

The compiler knows each callback's parameter types and generates a minimal event serializer — avoids shipping the full `Event` object across the boundary.

### Phase 6: Keyed List Reconciler

The one place that needs diffing — but only for lists, not the whole tree:

```saffron
UI.each(items, key: fun (item) => item.id) { item =>
    TodoItem(item)
}
```

Implements a keyed reconciliation algorithm (similar to Solid's `<For>`).

### Phase 7: Router, Suspense, Resource Loading

Framework completeness:

```saffron
fun UserProfile(id: Int): UI.Element {
    var user = UI.resource(fun () => fetchUser(id))

    match (user.get()) {
        is Loading => Spinner()
        is Ready(u) => div { h1 { u.name } }
        is Error(e) => p { "Failed: ${e}" }
    }
}
```

- Client-side router with pattern matching on URL segments
- `UI.resource()` integrates async data fetching with suspense boundaries
- Cooperative async model maps naturally to loading states

### Phase 8: HTML Template Syntax (Sugar)

A late-stage ergonomic addition. HTML syntax desugars to builder calls at parse time:

```saffron
// User writes:
<div class="counter">
    <h1>Count: {count}</h1>
    <button onClick={increment}>+</button>
</div>

// Parser desugars to:
div(class: "counter") {
    h1 { "Count: ${count}" }
    button(on_click: increment) { "+" }
}
```

Desugaring rules:

| HTML | Builder |
|------|---------|
| `<tag attr="value">` | `tag(attr: "value") {` |
| `</tag>` | `}` |
| `<tag />` | `tag()` |
| `{expr}` | `expr` |
| text content | `"text content"` |
| `<Component prop={x}>` | `Component(prop: x) {` |

The framework is unaware — it only sees function calls with trailing blocks regardless of source syntax. Both syntaxes can coexist in the same file.

## Key Risks

- **GC pauses** — mark-sweep stops the world. For 60fps UI, pauses must stay under 16ms. May need incremental/generational GC for the WASM target.
- **Binary size** — full runtime + GC + stdlib in WASM. Expect 300KB+ minimum. LTO helps.
- **String marshalling cost** — every string crossing the boundary requires copy + encoding conversion. Batch DOM operations where possible.
- **Debugging** — WASM source maps are painful. Consider a dev mode that outputs JS instead.

## Prior Art

| Framework | Approach | Lesson for Saffron |
|-----------|----------|-------------------|
| Leptos (Rust→WASM) | Signals + fine-grained reactivity | Closest analog — validates the approach |
| Yew (Rust→WASM) | VDOM, ~800KB binaries | Avoid VDOM — too expensive across WASM boundary |
| Solid.js | Signals + compile-time templates | Performance model we're targeting |
| Blazor (C#→WASM) | Ships whole .NET runtime, 2MB+ | Cautionary tale for binary size |
| SwiftUI / Compose | Builder DSL | Syntax model we're following |

## Why This Works for Saffron

- **Closures** → component functions
- **Enums + pattern matching** → conditional rendering (`match` on state)
- **Generics** → typed props and signals (`Signal<Int>`)
- **Interfaces** → component contracts
- **Trailing closures** → builder-style element nesting
- **Cooperative async** → data fetching / suspense
- **String interpolation** → text content in templates
- **Type system** → compile-time validation of attributes and props
