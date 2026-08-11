# Sumac

Sumac is a reactive terminal-UI (TUI) framework for [Saffron](../introduction.md).
It is to the terminal what [Turmeric](../turmeric/introduction.md) is to the web:
the same fine-grained reactivity (signals) and the same builder-style DSL, but it
renders to a terminal through a **cell buffer + ANSI diff** instead of the DOM.

## Design Principles

- **Signals over redraw loops** — fine-grained reactivity rebuilds the view when
  a signal changes; a minimal diff turns that into the smallest possible write to
  the terminal.
- **Builder syntax** — `box(border=2) { vstack { text("hi") } }` via trailing
  closures and named args, not a template language.
- **Reactive, not MVU** — there is no `Model`/`Update`/`Cmd`/`Msg`. State lives in
  signals you own; input handlers mutate them; the view is a pure function of
  them. "Bubbletea parity" means *visual/rendering* parity, not API parity.
- **One thin native layer** — everything above the terminal (style, buffer,
  layout, widgets, runtime) is pure Saffron. Only `tty.sf` touches the OS, via
  `src/runtime/tty_native.c`.

## Quick Example

```saffron
import { signal } from "sumac/signal"
import { vstack, box, text } from "sumac/layout"
import { run, on_key, quit } from "sumac/runtime"

var count = signal(0)

fun view() {
    box(border=2, title="counter") {
        vstack {
            text("count: ${count.get()}")
            text("press + / -, q to quit")
        }
    }
}

on_key("+", fun () => count.set(count.get() + 1))
on_key("-", fun () => count.set(count.get() - 1))
on_key("q", quit)

run(view)
```

## Architecture

```
builders push LNodes onto a context stack   (layout.sf / widgets.sf)
        │  (block runs, children collected, pop)
        ▼
   LNode tree
        │  layout pass: measure + position into regions
        ▼
   paint pass: rasterize styled runes into a Buffer   (buffer.sf)
        │  diff back-buffer vs front-buffer
        ▼
   minimal ANSI ops → tty.write()                     (tty.sf)
```

See [Architecture](./architecture.md) for the full pipeline, including the four
ideas reused from Turmeric and the one terminal-specific layer.

## Status

Sumac is under active development. The module graph and its state:

| Module | Role | Status |
|--------|------|--------|
| `tty.sf` | raw mode, alt-screen, size, input read, ANSI constants | **built** |
| `style.sf` | `Color`, `Style`, attributes, adaptive, downsample | **built** |
| `buffer.sf` | `Cell`, `Buffer`, rune width, minimal `diff` | **built** |
| `input.sf` | key/mouse/paste/focus escape-sequence parser | **built** |
| `signal.sf` | `signal`/`computed`/`effect`/`batch`/`untrack` | **built** |
| `layout.sf` | `LNode`, builders, layout + paint | *in progress (spec: `CONTRACT.md`)* |
| `widgets.sf` | list, viewport, textinput, … | *in progress (spec: `CONTRACT.md`)* |
| `runtime.sf` | `run`, `on_key`, `on_mouse`, `quit`, `request_render` | *in progress (spec: `CONTRACT.md`)* |

Throughout these docs, signatures pulled from a **built** module are exact.
Signatures for the in-progress modules are marked **API (from spec)** — they
follow `sumac/CONTRACT.md` and may shift as the modules land.

## Native only

Sumac drives a real terminal (raw mode, cursor control, mouse reporting) through
`tty_native.c`. There is no wasm target: on wasm there is no controlling
terminal, the TTY externs return failure, and `size()` falls back to 80×24. Build
Sumac apps as **native binaries** with `tools/saffron build`.
</content>
