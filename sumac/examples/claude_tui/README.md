# claude_tui — the Sumac showcase

A prettier, reactive terminal front-end for the `claude` CLI, built entirely on
[Sumac](../../) primitives. This is the flagship example: it exercises signals,
the flexbox layout engine, truecolor styling, the `viewport` / `textarea` /
`spinner` / `help` widgets, `@process` subprocess plumbing, and the reactive
render loop — all under normal GC (no `GC.disable()`).

It implements the **"Saffron Dusk"** design in [`DESIGN.md`](./DESIGN.md): a warm,
low-contrast, editorial palette (deep ember/amber over warm-black charcoal), with
speaker sigils (`▌ you` sage, `◈ claude` saffron), a rounded amber message well,
and a footer keymap.

## Run it

```bash
# from the saffron repo root
tools/saffron run   sumac/examples/claude_tui/src/main.sf          # compile + run
tools/saffron build sumac/examples/claude_tui/src/main.sf -o claude-tui
```

Type a prompt and press **Enter**. The app appends your message, spawns
`claude -p "<your prompt>"` via `@process`, and appends the reply. If `claude`
isn't on your `PATH`, it appends a non-fatal error message instead of crashing.

## Keys

| Key | Action |
|-----|--------|
| `⏎` Enter | Send the current prompt to `claude` |
| `↑` / `↓` | Scroll the transcript one line |
| `PgUp` / `PgDn` | Scroll one page |
| `^C` / `^D` | Quit (restores the terminal cleanly) |
| any printable | Types into the prompt editor |

## Render test

`test_render.sf` builds the app's real `view()` once into a `Buffer(100, 30)` and
dumps it to text — a golden-style, TTY-free way to eyeball the layout:

```bash
tools/saffron run sumac/examples/claude_tui/test_render.sf
```

## Files

- `src/app.sf` — all UI state (signals), the "Saffron Dusk" palette, the flat
  transcript model + word wrap, the `view()` builder, and the `send` /
  `_run_claude` subprocess logic. No top-level side effects, so both the loop and
  the render test share one source of truth.
- `src/main.sf` — the entry point: sizes the terminal, focuses the prompt,
  registers key handlers, and enters `Rt.run(App.view)`.
- `test_render.sf` — one-shot render-to-text harness.

## Notes / known limits

- **v1 is a one-shot call, not a live stream.** `claude` is invoked with
  `Process.exec` and the whole reply is appended when it returns. The synchronous
  render loop paints a "thinking…" frame (thick input border + spinner) before the
  blocking call. Token-by-token streaming via a background `Task` (as DESIGN §6
  sketches) is the natural next step.
- **Rendering lives in `app.sf`, including the render test's buffer dump.**
  Saffron keys module instances by the literal import-path string, so a file
  reaching `layout.sf` through a different relative-path spelling than `app.sf`
  uses would get a *separate* layout instance whose `_collect` context never sees
  `app.sf`'s emitted nodes. Everything therefore renders through `app.sf`'s single
  instance.
- The command palette, tool-call panels, token budget bar, and theme cycling from
  DESIGN are not yet wired — the core chat flow is the focus of v1.
