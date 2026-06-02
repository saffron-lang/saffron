# Turmeric

Turmeric is a reactive web framework for [Saffron](../introduction.md) that compiles to WebAssembly. It uses fine-grained reactivity (signals) and a builder-style DSL to create web applications with no virtual DOM overhead.

## Design Principles

- **Signals over VDOM** — fine-grained reactivity issues surgical DOM updates, avoiding expensive diffing
- **Builder syntax** — `div(cls="card") { children }` via trailing closures and named args, not JSX
- **Compile-time analysis** — the compiler statically tracks signal dependencies
- **Minimal JS glue** — a small JS runtime manages a DOM handle table; everything else runs in WASM
- **Type-safe from the spec** — element builders and event types are generated from the TypeScript DOM lib

## Quick Example

```saffron
import { signal, computed, effect } from "turmeric/signal"
import { Event } from "turmeric/events"

var count = signal(0)

fun App() {
    div(cls="counter") {
        h1 { reactive(fun () => "Count: " + count.get().to_string()) }
        button(cls="btn", on_click=fun (e: Event) => count.set(count.get() + 1)) {
            "Increment"
        }
    }
}

mount("#app", App)
```

## Architecture

```
                                              
 Saffron       LLVM IR         .wasm  
 source   -->  (wasm32)   -->  binary 
                                              
                                 |
                 ----------------+
                 v
 Browser                          
  +----------+  +--------+  
  | WASM     |<>| JS glue|  
  | (signals,|  | (DOM   |  
  |  render) |  |  ops)  |  
  +----------+  +--------+  
                                     
```

## Status

Turmeric is in active development. The following are implemented:

- Signal reactivity (`signal`, `computed`, `effect`)
- Element builder functions (all 112 HTML elements)
- Named-arg attributes (`cls`, `id`, `on_click`, `type_`, etc.)
- Event handlers with typed `Event` objects
- Conditional class maps with `cx()`
- Reactive text with `reactive(fun () => ...)`
- List rendering with `each()`
- WASM compilation and DOM interop bridge
- Client-side page routing via signal-based match
- The `mount()` function for attaching to a DOM element

## Building a Turmeric app

Turmeric apps use the `pantry` build tool:

```bash
cd examples/turmeric_app
pantry build
```

This compiles the Saffron source to wasm32, links the turmeric runtime, and produces a `.wasm` binary served with a tiny HTML+JS loader.
