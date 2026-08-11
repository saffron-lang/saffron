# Sumac

**Sumac is a reactive TUI framework for [Saffron](https://github.com/saffron-lang/saffron).**
It is to the terminal what [Turmeric](../docs/src/turmeric/introduction.md) is to
the web: the same Compose-like builder syntax and the same signals-based
reactivity, but it renders to a terminal through a cell buffer + ANSI diff
instead of the DOM.

You describe *what the screen should look like* as a function of your signals.
When a signal changes, Sumac rebuilds the view, paints it into an off-screen
cell buffer, diffs that against what the terminal is already showing, and writes
only the bytes that changed. The result is flicker-free, truecolor, mouse-aware
terminal UI — without the `Cmd`/`Msg`/`Update` plumbing of an MVU framework.

```
builders → LNode tree → layout pass → paint into Buffer → diff vs front buffer → ANSI to tty
```

## Hello, counter

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

`view` reads `count.get()`, so Sumac re-runs it whenever the signal changes and
repaints only the cells that differ. No manual redraw, no message enum.

> The exact import spellings (`sumac/signal`, `sumac/layout`, `sumac/runtime`)
> mirror Turmeric's package-relative module convention. `signal.sf`, `style.sf`,
> `buffer.sf`, `input.sf`, and `tty.sf` are built; `layout.sf`, `widgets.sf`, and
> `runtime.sf` are still being assembled, so the builder/runtime surface follows
> `CONTRACT.md`. See the docs for what is verified vs. specified.

## Install / build / run

Sumac is a `pantry` library (see `pantry.toml`). Add it as a dependency of your
app, then build a native binary with the Saffron toolchain:

```bash
# Compile + link + run a program directly (quick iteration):
tools/saffron run app.sf

# Build a standalone native binary:
tools/saffron build app.sf -o app
./app
```

Sumac is **native-only** — it drives a real terminal through
`src/runtime/tty_native.c` (raw mode, size query, input read). On wasm there is
no controlling terminal, so the TTY externs fail gracefully and `size()` falls
back to 80×24; the rendering core (style/buffer/layout) still runs, but there is
nothing to render *to*.

## Features

- **Fine-grained reactivity** — `signal`, `computed`, `effect`, `batch`,
  `untrack`; the same primitives Turmeric uses, tracked automatically.
- **Compose-like builders** — `vstack` / `hstack` / `box` / `text` / `spacer`
  assembled with trailing closures over a context stack. No template language.
- **Truecolor styling** — 24-bit `rgb()`/`hex()`, ANSI-256, ANSI-16, adaptive
  light/dark colors, and automatic **downsampling** to whatever the terminal
  supports. Bold, faint, italic, underline, blink, reverse, strike.
- **Flexbox-ish layout** — direction, grow/flex, fixed sizes, padding, cross-
  and main-axis alignment (including space-between), five border styles, titles.
- **Full input** — key events with canonical names (`ctrl+c`, `up`, `enter`,
  `F5`, `alt+x`), SGR mouse (click/drag/motion/wheel), bracketed paste, focus
  in/out, and terminal resize.
- **A widget set** — list, viewport, textinput, textarea, spinner, progress,
  table, tabs, paginator, help.
- **Minimal-diff renderer** — an off-screen cell buffer diffed against the live
  screen; only changed cells are emitted, with wide-character (CJK/emoji)
  awareness.

## Parity with Bubbletea / Lip Gloss / Bubbles

Sumac targets **visual and rendering parity** with the Charm stack — truecolor,
box-drawing borders, flex layout, mouse/paste/resize, and a comparable widget
gallery. It does **not** copy Bubbletea's architecture: there is no `Model`,
`Update`, `Cmd`, or `Msg`. Sumac is **reactive** (signals + effects), not MVU.
State lives in signals you own; handlers mutate them; the view is a pure function
of them. See [the comparison guide](../docs/src/sumac/comparison.md) for a full
concept-by-concept mapping.

## Documentation

Full docs live in the Saffron book under `docs/src/sumac/`:

- [Introduction](../docs/src/sumac/introduction.md)
- [Architecture](../docs/src/sumac/architecture.md)
- [Sumac Quickstart](../docs/src/guides/sumac-quickstart.md)
- [Styling & Color](../docs/src/sumac/styling.md)
- [Layout](../docs/src/sumac/layout.md)
- [Reactivity](../docs/src/sumac/reactivity.md)
- [Input Handling](../docs/src/sumac/input.md)
- [Widgets](../docs/src/sumac/widgets.md)
- [API Reference](../docs/src/sumac/api-reference.md)
- [Comparison to Bubbletea](../docs/src/sumac/comparison.md)

A flagship example — a reactive front-end for the `claude` CLI — is designed in
`examples/claude_tui/DESIGN.md`.
</content>
</invoke>
