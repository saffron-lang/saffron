# Comparison to Bubbletea / Lip Gloss / Bubbles

Sumac targets **visual and rendering parity** with the [Charm](https://charm.sh)
stack — the look and capabilities of a Bubbletea app — but it does **not** copy
Bubbletea's architecture. Sumac is **reactive** (signals + effects), not MVU
(Model / Update / Cmd / Msg). This page maps the concepts and is honest about
what is the same, what differs, and what Sumac deliberately does not do.

## Concept mapping

| Charm stack | Sumac equivalent |
|-------------|------------------|
| **Bubbletea** `Model` struct (all app state) | signals you own (`signal(...)`) |
| `Init() (Model, Cmd)` | plain setup: create signals, register `on_key`, then `run(view)` |
| `Update(msg) (Model, Cmd)` | input handlers that mutate signals (`on_key`, `on_mouse`, `on_event`) |
| `View() string` | `view()` builder function reading signals |
| `tea.Msg` / `tea.Cmd` (async plumbing) | *(none)* — mutate a signal; the view re-runs. Async via Saffron `Task.spawn` |
| `p.Send(msg)` / `tea.Batch` | `signal.set(...)`; `batch(fun () => …)` for coalescing |
| `tea.Quit` | `quit()` |
| `tea.WindowSizeMsg` | `Event.resize` (tree reflows automatically) |
| `tea.KeyMsg` / `key.Matches` | `KeyEvent.name()` + `on_key("ctrl+c", …)` |
| `tea.MouseMsg` | `MouseEvent` + `on_mouse(…)` |
| bracketed paste msg | `Event.paste` |
| **Lip Gloss** `Style` | `style.sf` `Style` (immutable, fluent builders) |
| Lip Gloss `Color` / `AdaptiveColor` | `rgb`/`hex`/`ansi256`/`ansi`; `adaptive(light, dark, is_dark)` |
| Lip Gloss color profile degradation | `downsample(color, max_kind)` |
| Lip Gloss `JoinVertical` / `JoinHorizontal` | `vstack` / `hstack` |
| Lip Gloss `Border` / `Padding` / `Align` | `box(border=…, pad_*=…, align=…, justify=…)` |
| Lip Gloss `Place` (overlay) | `place(node, buf, x, y, w, h)` |
| **Bubbles** `list` | `list` widget |
| Bubbles `viewport` | `viewport` widget |
| Bubbles `textinput` | `textinput` widget |
| Bubbles `textarea` | `textarea` widget |
| Bubbles `spinner` | `spinner` widget |
| Bubbles `progress` | `progress` widget |
| Bubbles `table` | `table` widget |
| Bubbles `paginator` | `paginator` widget |
| Bubbles `help` | `help` widget |
| Bubbles `tabs` (community) | `tabs` widget |
| `tcell` / renderer | `buffer.sf` cell grid + minimal `diff` |

## What's the same

- **The rendered result.** Truecolor / 256 / 16 color with automatic
  downsampling, box-drawing borders (normal, rounded, thick, double), flexbox-ish
  layout with alignment and grow, wide-character (CJK/emoji) awareness, and a
  flicker-free minimal-diff renderer. A Sumac app can look like a Bubbletea app.
- **Input capabilities.** Keys with canonical names (`ctrl+c`, `up`, `f5`,
  `alt+x`), SGR mouse (click/drag/motion/wheel), bracketed paste, focus in/out,
  and resize.
- **The widget vocabulary.** list, viewport, textinput, textarea, spinner,
  progress, table, tabs, paginator, help — the Bubbles set.
- **Styling ergonomics.** An immutable `Style` value with fluent, chainable
  builders — the Lip Gloss feel.

## What's different

- **Reactive, not MVU.** There is no `Model`/`Update`/`Cmd`/`Msg`. State is a set
  of signals; handlers mutate them; the view is a pure function of them; the
  runtime re-renders on change. You never thread state through an update function
  or return commands.
- **No `Cmd`/`Msg` async plumbing.** Bubbletea models side effects and
  concurrency as `Cmd`s that produce `Msg`s fed back into `Update`. Sumac has no
  such channel: for background work, spawn a Saffron `Task` and have it `set` a
  signal when done — the set triggers a repaint. (The flagship `claude_tui`
  example drains a subprocess's stdout in a `Task.spawn` and appends to a message
  signal.)
- **Builders over string concatenation.** A Bubbletea `View()` returns a
  `string` you assemble (often with Lip Gloss joins). A Sumac `view()` calls
  builders that push onto a context stack; layout and painting are the
  framework's job, not yours.
- **State ownership is explicit and shared.** Widgets don't hide their state in
  an opaque model — you pass in the signals they bind to, so the same `selected`
  signal a `list` uses can drive a `computed` elsewhere in your UI.

## What we don't do

- **No MVU / Elm architecture.** By design. If you want `Update`/`Cmd`, that is
  not Sumac's model.
- **No Go ecosystem interop.** Bubbles/Lip Gloss/Bubbletea are Go libraries;
  Sumac is Saffron. There is no shared code, plugin, or `tea.Program` interop —
  only conceptual parity.
- **No `tea.Program` options surface (yet).** Alt-screen, mouse, paste, and focus
  reporting are turned on by `tty.enter()` as a fixed full-screen profile rather
  than a menu of program options.
- **No web/wasm target.** Sumac is native-only (it needs a real controlling
  terminal). For the browser, use [Turmeric](../turmeric/introduction.md), whose
  reactive model is the same.
</content>
