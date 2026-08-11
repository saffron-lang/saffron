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

# or build the stable binary once and run it by name:
bash sumac/examples/claude_tui/rebuild.sh   # → sclaude-bin
```

Type a prompt and press **Enter**. The app appends your message, spawns
`claude` via `@process` (streaming stream-json), and folds the reply in live. If
`claude` isn't on your `PATH`, it appends a non-fatal error message instead of
crashing.

### Conversations (session continuity)

Every turn within a single run now shares **one** `claude` conversation, so
Claude keeps full context from turn to turn. The first prompt starts a fresh
session; its id is captured from the stream's `system/init` event (shown as
`⟐ session <short-id>` in the header), and every later prompt is sent with
`--resume <id>`.

Launch flags pick which conversation you start in:

| Command | Behavior |
|---------|----------|
| `sclaude` | Start a **new** conversation. |
| `sclaude --continue` / `sclaude -c` | Resume the **most recent** conversation (first turn passes `--continue`; the resulting session id then drives the rest of the run). |
| `sclaude --resume` | Open an interactive **picker** of recent conversations before the chat. |
| `sclaude --resume=<id>` | Resume that session **directly**, skipping the picker. |

**The resume picker** scans `~/.claude/projects/*/*.jsonl` across *all* projects
(one JSONL file per conversation), newest-first, capped at the **50** most recent
so the scan stays snappy. Each row's label is the conversation's first real user
message (only the first ~40 lines of each file are read, and system/command
pseudo-messages are skipped; the session id is used as a fallback when no user
text is found). In the picker: `↑↓` select · type to `/`filter · `⏎` resume the
highlighted conversation · `esc` cancel and start a new session.

## Keys

| Key | Action |
|-----|--------|
| `⏎` Enter | Send the current prompt (in the picker: resume the highlighted conversation) |
| `Esc` | Interrupt a running generation (in the picker: cancel → new session) |
| `↑` / `↓` | Scroll the transcript one line (in the picker: move the selection) |
| `PgUp` / `PgDn` | Scroll one page (in the picker: page the selection) |
| `Shift+Tab` | Cycle the permission-mode indicator |
| `^L` | Clear the transcript |
| `^C` / `^D` | Quit (restores the terminal cleanly) |
| any printable | Types into the prompt editor (in the picker: narrows the filter) |

## Render test

`test_render.sf` builds the app's real `view()` once into a `Buffer(100, 30)` and
dumps it to text — a golden-style, TTY-free way to eyeball the layout:

```bash
tools/saffron run sumac/examples/claude_tui/test_render.sf
```

Two more headless harnesses cover the streaming/session logic without a TTY:

```bash
tools/saffron run sumac/examples/claude_tui/test_stream.sf   # stream-json pump,
    # session continuity (first-turn vs --resume argv), spinner-repaint, panels
tools/saffron run sumac/examples/claude_tui/test_picker.sf   # session scan +
    # first-user-message preview + selection→resume + filter (stub jsonl files)
```

`test_picker.sf` reads stub session files from `/tmp/ct_sessions/*.jsonl` — see
the top of the file for the shape it expects.

## Files

- `src/app.sf` — all UI state (signals), the "Saffron Dusk" palette, the flat
  transcript model + word wrap, the `view()` builder, and the `send` /
  `_run_claude` subprocess logic. No top-level side effects, so both the loop and
  the render test share one source of truth.
- `src/main.sf` — the entry point: sizes the terminal, focuses the prompt,
  registers key handlers, and enters `Rt.run(App.view)`.
- `test_render.sf` — one-shot render-to-text harness.

## Notes / known limits

- **Streaming is live and non-blocking.** `claude` is spawned with
  `--output-format stream-json --verbose`; an `Rt.on_tick` pump reads whatever
  stdout is available each ~50ms loop iteration and folds the structured events
  (assistant text, tool calls, subagent spawns, final result) into the transcript
  as they arrive. The spinner animates ~20Hz throughout — while a run is active
  the pump requests a repaint every tick, not only when new data lands, so long
  "thinking" gaps still animate smoothly; when idle the loop stays quiet.
- **Sessions persist across turns within a run** (see *Conversations* above): the
  session id is captured from `system/init` and threaded through `--resume`.
- **Rendering lives in `app.sf`, including the render test's buffer dump.**
  Saffron keys module instances by the literal import-path string, so a file
  reaching `layout.sf` through a different relative-path spelling than `app.sf`
  uses would get a *separate* layout instance whose `_collect` context never sees
  `app.sf`'s emitted nodes. Everything therefore renders through `app.sf`'s single
  instance.
- The command palette, tool-call panels, token budget bar, and theme cycling from
  DESIGN are not yet wired — the core chat flow is the focus of v1.
