# API Reference

Public functions and classes per module. Signatures for **built** modules
(`tty`, `style`, `buffer`, `input`, `signal`) are exact. Signatures for
**API (from spec)** modules (`layout`, `widgets`, `runtime`) follow
`sumac/CONTRACT.md` and may change as those modules land.

---

## `tty` — built

Raw mode, alternate screen, cursor, size, and the low-level input read. A thin
Saffron skin over `src/runtime/tty_native.c`.

### Class `TermSize`

| Member | Signature | Description |
|--------|-----------|-------------|
| field | `rows: Int` | terminal height in cells |
| field | `cols: Int` | terminal width in cells |
| `init` | `init(rows: Int, cols: Int)` | construct |

### Functions

| Signature | Description |
|-----------|-------------|
| `size(): TermSize` | current terminal size; falls back to 80×24 with no tty |
| `write(s: String)` | write a control/render string to stdout, unbuffered |
| `move_to(row: Int, col: Int): String` | ANSI string to move cursor (1-based) |
| `goto(row: Int, col: Int)` | move the cursor immediately |
| `read_input(max_len: Int, timeout_ms: Int): IO.Bytes` | read input bytes (timeout ms: <0 blocks, 0 polls) |
| `enter()` | enter full-screen UI mode (raw, alt-screen, hidden cursor, mouse/paste/focus); idempotent |
| `leave()` | restore the terminal, reversing every `enter()` step; idempotent |

### Constants

`ESC`, `CSI`, `ALT_SCREEN_ON`/`OFF`, `CURSOR_HIDE`/`SHOW`, `CLEAR_SCREEN`,
`CLEAR_LINE`, `CURSOR_HOME`, `RESET_STYLE`, `MOUSE_ON`/`OFF`, `PASTE_ON`/`OFF`,
`FOCUS_ON`/`OFF` — the named ANSI control strings the renderer emits.

---

## `style` — built

Immutable colors and text styles that serialize to SGR escapes.

### Class `Color`

| Member | Signature | Description |
|--------|-----------|-------------|
| fields | `kind: Int, idx: Int, r: Int, g: Int, b: Int` | `kind`: 0 default, 1 ansi16, 2 ansi256, 3 truecolor |
| `init` | `init(kind, idx, r, g, b)` | construct |
| `sgr` | `sgr(fg: Bool): String` | SGR parameter run for fg (`true`) or bg |

### Class `Style`

| Member | Signature | Description |
|--------|-----------|-------------|
| fields | `fg: Color, bg: Color, attrs: Int` | resolved style a `Cell` stores |
| `init` | `init(fg, bg, attrs)` | construct |
| `sgr` | `sgr(): String` | full SGR string, leading with reset (`0`) |
| `eq` | `eq(other: Style): Bool` | structural equality (used by diff) |
| `with_fg` | `with_fg(c: Color): Style` | new style with fg set |
| `with_bg` | `with_bg(c: Color): Style` | new style with bg set |
| `bold`/`faint`/`italic`/`underline`/`blink`/`reverse`/`strike` | `(): Style` | new style with the attribute added |

### Functions

| Signature | Description |
|-----------|-------------|
| `color_default(): Color` | terminal default fg/bg (no SGR) |
| `ansi(idx: Int): Color` | ANSI-16 color, `idx` 0..15 |
| `ansi256(idx: Int): Color` | ANSI-256 color, `idx` 0..255 |
| `rgb(r: Int, g: Int, b: Int): Color` | 24-bit truecolor |
| `hex(s: String): Color` | truecolor from `"#rrggbb"` / `"#rgb"` |
| `style(): Style` | default style |
| `adaptive(light: Color, dark: Color, is_dark: Bool): Color` | pick a variant by background |
| `downsample(c: Color, max_kind: Int): Color` | approximate `c` to the richest supported kind |

### Constants

`ATTR_NONE` 0, `ATTR_BOLD` 1, `ATTR_FAINT` 2, `ATTR_ITALIC` 4,
`ATTR_UNDERLINE` 8, `ATTR_BLINK` 16, `ATTR_REVERSE` 32, `ATTR_STRIKE` 64.

---

## `buffer` — built

The cell grid and the minimal-diff renderer.

### Class `Cell`

| Member | Signature | Description |
|--------|-----------|-------------|
| fields | `rune: String, style: Style, width: Int` | `rune` is a 1-grapheme string; `width` 1 / 2 (wide) / 0 (wide trailing half) |
| `init` | `init(rune, style, width)` | construct |

### Class `Buffer`

| Member | Signature | Description |
|--------|-----------|-------------|
| fields | `w: Int, h: Int` | grid dimensions (`cells` is internal) |
| `init` | `init(w: Int, h: Int)` | fill with blanks in the default style |
| `get` | `get(x: Int, y: Int): Cell` | bounds-safe read (off-grid → blank) |
| `set` | `set(x: Int, y: Int, c: Cell)` | bounds-safe write (off-grid → no-op) |
| `put_str` | `put_str(x: Int, y: Int, s: String, st: Style): Int` | write a string, advancing by display width, clipping at the edge; returns the x past the last cell |
| `fill` | `fill(x: Int, y: Int, w: Int, h: Int, c: Cell)` | rect fill |
| `clear` | `clear()` | reset every cell to a blank |
| `resize` | `resize(w: Int, h: Int)` | resize, preserving the top-left overlap |

### Functions

| Signature | Description |
|-----------|-------------|
| `rune_width(rune: String): Int` | display columns for one rune (0 / 1 / 2) |
| `str_width(s: String): Int` | display columns for a whole string |
| `diff(front: Buffer, back: Buffer, force: Bool): String` | minimal ANSI stream transforming front → back; `force` = full repaint. Mutates neither buffer |

---

## `input` — built

Escape-sequence parser: raw bytes → high-level events.

### Class `KeyEvent`

| Member | Signature | Description |
|--------|-----------|-------------|
| fields | `kind: Int, rune: String, mods: Int` | `rune` set when `kind == KEY_RUNE` |
| `init` | `init(kind, rune, mods)` | construct |
| `name` | `name(): String` | canonical keymap name, e.g. `"ctrl+c"`, `"up"`, `"f5"` |

### Class `MouseEvent`

| Member | Signature | Description |
|--------|-----------|-------------|
| fields | `kind: Int, button: Int, x: Int, y: Int, mods: Int` | 0-based `x`/`y` |
| `init` | `init(kind, button, x, y, mods)` | construct |

### Class `Event`

| Member | Signature | Description |
|--------|-----------|-------------|
| fields | `key: KeyEvent, mouse: MouseEvent, paste: String, resize: Bool, focus: Int` | exactly one facet populated |
| `init` | `init(key, mouse, paste, resize, focus)` | construct |

### Functions

| Signature | Description |
|-----------|-------------|
| `parse(bytes: IO.Bytes): List<Event>` | decode zero or more events; stateless per call; drops unknown escapes |
| `key_event(kind, rune, mods): Event` | build a key event |
| `mouse_event(kind, button, x, y, mods): Event` | build a mouse event |
| `paste_event(text): Event` | build a paste event |
| `focus_event(focused): Event` | build a focus event (`0` blur / `1` focus) |
| `resize_event(): Event` | build a resize marker |

### Constants

Key kinds `KEY_RUNE`..`KEY_F12` (0..27); modifiers `MOD_NONE`/`CTRL`/`ALT`/`SHIFT`
(0/1/2/4); mouse kinds `MOUSE_PRESS`/`RELEASE`/`MOTION`/`WHEEL_UP`/`WHEEL_DOWN`
(0..4).

---

## `signal` — built

Reactive primitives (identical model to Turmeric's).

### Functions

| Signature | Description |
|-----------|-------------|
| `signal<T>(initial: T): Signal<T>` | create a mutable reactive value |
| `computed<T>(compute: () => T): Computed<T>` | create a derived reactive value |
| `effect(fn: () => Nil): EffectHandle` | run now + on every dependency change |
| `batch(fn: () => Nil)` | defer notifications until the outermost batch ends |
| `untrack<T>(fn: () => T): T` | read without recording dependencies |

### Class `Signal<T>`

| Member | Signature | Description |
|--------|-----------|-------------|
| `get` | `get(): T` | read (records a dependency) |
| `set` | `set(next: T)` | write and notify |
| `update` | `update(fn: (T) => T)` | read + write |
| `subscribe` | `subscribe(fn: () => Nil): Float` | subscribe; returns an id |
| `unsubscribe` | `unsubscribe(id: Float)` | remove a subscription |

### Class `Computed<T>`

`get(): T`, `recompute()`, `subscribe`/`unsubscribe` — same shape as `Signal<T>`,
value derived from `compute`.

### Class `EffectHandle`

`dispose()` — unsubscribe the effect from all its dependencies.

---

## `layout` — API (from spec)

Retained-tree builders, layout, and paint. See [Layout](./layout.md).

### Enum `LNode`

`TextNode(content: String, style: Style)` | `BoxNode(props: BoxProps, children: List<LNode>)`

### Class `BoxProps`

`dir`, `border`, `pad_t`/`pad_r`/`pad_b`/`pad_l`, `align`, `justify`,
`width`, `height`, `grow`, `style`, `title` — see the [Layout](./layout.md) table.

### Functions

| Signature | Description |
|-----------|-------------|
| `text(content: String)` | text run (plus styled variants) |
| `vstack(block: () => Nil)` | vertical container (plus param overloads) |
| `hstack(block: () => Nil)` | horizontal container |
| `box(...props..., block: () => Nil)` | bordered/padded/aligned container |
| `spacer()` | flexible gap |
| `_push_root(): LayoutCtx` | runtime entry: start a root context |
| `_collect(block: () => Nil): List<LNode>` | run a block, return the children built |
| `render_node(node, buf, x, y, w, h)` | measure + position + paint into a buffer |
| `place(node, buf, x, y, w, h)` | overlay a node at an absolute region (modals) |

---

## `widgets` — API (from spec)

Builder-function widgets that bind to your signals. See [Widgets](./widgets.md).
Set: `list`, `viewport`, `textinput`, `textarea`, `spinner`, `progress`,
`table`, `tabs`, `paginator`, `help`. Each owns state as a class holding signals,
with a `view()` builder and a `handle(e: Event)` input method; the constructor
takes the signals the widget binds to.

---

## `runtime` — API (from spec)

The event loop and handler registration. See the [Quickstart](../guides/sumac-quickstart.md).

### Functions

| Signature | Description |
|-----------|-------------|
| `run(view: () => Nil)` | enter the tty, loop (read → parse → dispatch → repaint on dirty), leave on quit |
| `on_key(name: String, handler: () => Nil)` | register a global key handler, e.g. `on_key("ctrl+c", quit)` |
| `on_mouse(handler: (MouseEvent) => Nil)` | register a mouse handler |
| `on_event(handler: (Event) => Nil)` | catch-all event handler |
| `quit()` | break the loop and restore the terminal |
| `request_render()` | request a redraw (for timer-driven animation) |
</content>
