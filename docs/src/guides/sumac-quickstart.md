# Sumac Quickstart

This guide walks you through creating and running your first Sumac terminal app.

## Prerequisites

- Saffron installed (see [Installation](../getting-started/installation.md))
- A terminal that supports ANSI escapes (any modern terminal emulator)
- For full color: a truecolor-capable terminal (Sumac downsamples gracefully if
  not)

Sumac is **native only** — it drives a real terminal through
`src/runtime/tty_native.c`. There is no wasm build.

## Create a project

Sumac apps are ordinary Saffron programs that depend on the `sumac` package.
Using `pantry`:

```bash
pantry new mytui
cd mytui
```

Add `sumac` as a dependency in `pantry.toml`:

```toml
[package]
name = "mytui"
version = "0.1.0"
type = "binary"
entry = "src/main.sf"
target = "native"

[dependencies]
sumac = "0.1.0"
```

## Write your app

Edit `src/main.sf`:

```saffron
import { signal } from "sumac/signal"
import { vstack, box, text } from "sumac/layout"
import { run, on_key, quit } from "sumac/runtime"

var count = signal(0)

fun view() {
    box(border=2, title="counter") {
        vstack {
            text("count: ${count.get()}")
            text("")
            text("press + to increment, - to decrement, q to quit")
        }
    }
}

fun main() {
    on_key("+", fun () => count.set(count.get() + 1))
    on_key("-", fun () => count.set(count.get() - 1))
    on_key("q", quit)
    run(view)
}

main()
```

`view` reads `count.get()`. Because the read is tracked, Sumac re-runs `view`
whenever `count` changes, repaints the off-screen buffer, and writes only the
cells that differ.

## Build & run

```bash
# Quick iteration — compile, link, and run in one step:
tools/saffron run src/main.sf

# Or build a standalone native binary:
tools/saffron build src/main.sf -o mytui
./mytui
```

`run(view)` calls `tty.enter()` (raw mode, alternate screen, hidden cursor,
mouse + paste + focus reporting), loops reading and dispatching input, and calls
`tty.leave()` to restore the terminal on `quit()`. Always let the runtime own
that lifecycle so a clean exit restores your terminal.

## How it works

1. **Builders** (`box`, `vstack`, `text`) push nodes onto a context stack; the
   trailing closure's children attach to the node above them. `view()` returns
   nothing — it *builds* a tree as a side effect.
2. **Signals** hold reactive state. Reading `count.get()` inside `view` subscribes
   the render to that signal.
3. **`on_key(name, handler)`** registers a global key handler. Names are the
   canonical forms from `KeyEvent.name()`: `"+"`, `"q"`, `"ctrl+c"`, `"up"`,
   `"enter"`, `"F5"`.
4. **`run(view)`** enters the terminal, runs the event loop, and repaints on
   change until `quit()`.

## Add some color

```saffron
import { signal } from "sumac/signal"
import { vstack, box, text_styled } from "sumac/layout"
import { style, rgb, hex } from "sumac/style"
import { run, on_key, quit } from "sumac/runtime"

var count = signal(0)

fun view() {
    var accent = style().with_fg(hex("#E0A54A")).bold()   // saffron, bold
    box(border=2, title="counter") {
        vstack {
            text_styled("count: ${count.get()}", accent)
        }
    }
}

fun main() {
    on_key("+", fun () => count.set(count.get() + 1))
    on_key("q", quit)
    run(view)
}

main()
```

`style()` returns a default style; the fluent builders (`with_fg`, `bold`, …)
each return a **new** `Style`. See [Styling & Color](../sumac/styling.md).

> The styled-text builder is shown here as `text_styled`. `CONTRACT.md` specifies
> `text(content)` "plus styled variants" without fixing the exact name, so treat
> the styled-builder spelling as **API (from spec)** until `layout.sf` lands.

## Handle more input

```saffron
import { on_key, on_mouse, on_event, quit } from "sumac/runtime"

on_key("ctrl+c", quit)
on_key("up", fun () => scroll_up())
on_key("down", fun () => scroll_down())

on_mouse(fun (e) => {
    // e.kind: 0 press, 1 release, 2 motion, 3 wheel-up, 4 wheel-down
    // e.x / e.y are 0-based cell coordinates
})

on_event(fun (e) => {
    if (e.resize) { /* the tree reflows automatically; hook extra work here */ }
    if (e.paste != nil) { /* e.paste holds the pasted text */ }
})
```

See [Input Handling](../sumac/input.md) for the full event model.

## Next steps

- [Architecture](../sumac/architecture.md) — how the pipeline fits together
- [Reactivity](../sumac/reactivity.md) — signals, computed, effects
- [Layout](../sumac/layout.md) — vstack/hstack/box, borders, padding, alignment
- [Styling & Color](../sumac/styling.md) — truecolor, attributes, downsampling
- [Widgets](../sumac/widgets.md) — list, viewport, textinput, spinner, and more
- [API Reference](../sumac/api-reference.md) — every public function per module
</content>
