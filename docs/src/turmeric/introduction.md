# Turmeric

Turmeric is a reactive web framework for [Saffron](../introduction.md) that compiles to WebAssembly. It uses fine-grained reactivity (signals) and a builder-style DSL to create web applications with no virtual DOM overhead.

## Design Principles

- **Signals over VDOM** — fine-grained reactivity issues surgical DOM updates, avoiding expensive diffing
- **Builder syntax** — `div([attrs]) { children }` via trailing closures, not JSX
- **Compile-time analysis** — the compiler statically tracks signal dependencies
- **Minimal JS glue** — a small JS runtime manages a DOM handle table; everything else runs in WASM
- **Type-safe from the spec** — element builders and event types are generated from the TypeScript DOM lib

## Quick Example

```saffron
import "turmeric" as T

fun Counter(): Node {
    var count = T.signal(0)

    return T.div {
        T.h1 { T.text("Count: ${count.get()}") }
        T.button([T.on_click(fun (e: PointerEvent) => {
            count.set(count.get() + 1)
        })]) {
            T.text("Increment")
        }
    }
}
```

## Architecture

```
┌─────────────┐     ┌──────────┐     ┌─────────┐
│  Saffron    │────▶│  LLVM IR │────▶│  .wasm  │
│  source     │     │ (wasm32) │     │  binary │
└─────────────┘     └──────────┘     └─────────┘
                                          │
                    ┌──────────────────────┘
                    ▼
┌─────────────────────────────┐
│  Browser                    │
│  ┌───────────┐  ┌────────┐ │
│  │ WASM      │◀▶│ JS glue│ │
│  │ (signals, │  │ (DOM   │ │
│  │  render)  │  │  ops)  │ │
│  └───────────┘  └────────┘ │
└─────────────────────────────┘
```

## Status

Turmeric is in early development. The following are implemented:

- ✅ Signal reactivity (`signal`, `computed`, `effect`)
- ✅ Element builder functions (all 112 HTML elements)
- ✅ Typed event handlers (from DOM spec)
- ✅ HTML string rendering (for SSR/testing)
- ✅ WASM runtime shim
- 🚧 WASM target in compiler
- 🚧 JS DOM interop bridge
- 🚧 Client-side router
- 🚧 CSS styling system
