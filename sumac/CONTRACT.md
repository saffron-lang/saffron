# Sumac — Internal Type Contract

This is the integration spec every module implements against. It exists so the
modules can be built in parallel without guessing each other's shapes. If you
need to change a signature here, say so explicitly — it affects other modules.

Sumac is a **reactive** TUI framework (signals/effects, like Turmeric — NOT
Bubbletea's MVU). "Bubbletea parity" means **visual/rendering capability
parity** (colors, borders, layout, widgets, mouse/paste/resize), not API parity.

## Architecture (retained tree — mirrors Turmeric)

```
builders push LNodes onto a context stack  (layout.sf / widgets.sf)
        │  (block runs, children collected, pop)
        ▼
   LNode tree
        │  layout pass: measure + position into regions
        ▼
   paint pass: rasterize styled runes into a Buffer   (buffer.sf)
        │  diff back-buffer vs front-buffer
        ▼
   minimal ANSI ops → tty.write()                     (tty.sf ✅ done)
```

The reactive runtime (runtime.sf) rebuilds the LNode tree when signals change,
re-paints into the back buffer, diffs, and flushes. Simplest-correct v1 marks
the whole frame dirty on any change; the diff keeps it flicker-free regardless.

## Module dependency graph

- `tty.sf` ✅ DONE — raw mode, size, input read, ANSI constants, enter/leave
- `style.sf` → (leaf) Color, Attr, Style
- `buffer.sf` → depends on style.sf: Cell, Buffer, diff
- `input.sf` → depends on io.Bytes only: Key, Mouse, Event, parse
- `layout.sf` → depends on buffer.sf + style.sf: LNode, builders, layout+paint
- `widgets.sf` → depends on layout.sf + buffer.sf + style.sf + signal.sf
- `runtime.sf` → depends on all + tty.sf + input.sf + signal.sf

## Shared value width note

Saffron numbers are `Float` unless annotated `Int`. Use `Int` for all buffer
indices, coordinates, byte values, and color components — a `Float`-typed list
index silently reads element 0 (BUGS #72). Coordinates are **0-based** inside
the buffer; ANSI cursor moves (1-based) are tty.sf's job.

---

## style.sf — CONTRACT

```saffron
// A color. Kind selects which fields matter:
//   0 = default (terminal's own fg/bg)
//   1 = ANSI 16   (idx 0..15)
//   2 = ANSI 256  (idx 0..255)
//   3 = truecolor (r,g,b each 0..255)
class Color {
    var kind: Int
    var idx: Int
    var r: Int
    var g: Int
    var b: Int
    fun init(kind: Int, idx: Int, r: Int, g: Int, b: Int)
    // Emit the SGR parameter list for this color as fg (fg=true) or bg.
    // e.g. truecolor fg -> "38;2;R;G;B", ansi256 bg -> "48;5;N". Default -> "".
    fun sgr(fg: Bool): String
}
// Constructors (free functions):
fun color_default(): Color
fun ansi(idx: Int): Color            // 0..15
fun ansi256(idx: Int): Color         // 0..255
fun rgb(r: Int, g: Int, b: Int): Color
fun hex(s: String): Color            // "#rrggbb" or "#rgb"

// Attribute bitflags (combine with +/bit-or via helper):
let ATTR_NONE: Int = 0
let ATTR_BOLD: Int = 1
let ATTR_FAINT: Int = 2
let ATTR_ITALIC: Int = 4
let ATTR_UNDERLINE: Int = 8
let ATTR_BLINK: Int = 16
let ATTR_REVERSE: Int = 32
let ATTR_STRIKE: Int = 64

// A resolved text style. This is what a Cell stores and what paint reads.
class Style {
    var fg: Color
    var bg: Color
    var attrs: Int
    fun init(fg: Color, bg: Color, attrs: Int)
    // The full SGR string to SET this style from a reset baseline,
    // e.g. "\x1b[0;1;38;2;255;0;0m". Always starts from 0 (reset) for
    // determinism; the diff optimizer may later suppress redundant emits.
    fun sgr(): String
    fun eq(other: Style): Bool       // for diffing / dedup
    // Fluent builders return a NEW Style (immutable style values):
    fun with_fg(c: Color): Style
    fun with_bg(c: Color): Style
    fun bold(): Style
    fun underline(): Style
    // ...one per attribute
}
fun style(): Style                   // default fg/bg, no attrs
```

## buffer.sf — CONTRACT

```saffron
import "./style.sf" as St

// One terminal cell. `rune` is a 1-grapheme String ("" or " " = blank).
// `width` is display columns: 1 normally, 2 for wide (CJK/emoji), 0 for the
// trailing half of a wide cell (painted as a no-op placeholder).
class Cell {
    var rune: String
    var style: St.Style
    var width: Int
    fun init(rune: String, style: St.Style, width: Int)
}

class Buffer {
    var w: Int
    var h: Int
    fun init(w: Int, h: Int)         // fills with blank cells, default style
    fun get(x: Int, y: Int): Cell    // bounds-safe: off-grid returns a blank
    fun set(x: Int, y: Int, c: Cell) // bounds-safe no-op if off-grid
    // Convenience: write a string starting at (x,y) with a style, clipping at
    // the right edge and advancing by each rune's display width. Returns the x
    // after the last written cell.
    fun put_str(x: Int, y: Int, s: String, st: St.Style): Int
    fun fill(x: Int, y: Int, w: Int, h: Int, c: Cell)  // rect fill
    fun clear()                      // reset to blanks
    fun resize(w: Int, h: Int)
}

// Compute the display width of a string in terminal columns (wide-char aware).
fun str_width(s: String): Int
fun rune_width(rune: String): Int

// Diff `back` against `front` and return the ANSI byte stream that transforms
// the terminal from front to back. Emits cursor moves + styled runs, minimal.
// After emitting, the caller copies back→front. `force` ignores front (full
// repaint, used on first frame / after resize).
fun diff(front: Buffer, back: Buffer, force: Bool): String
```

## input.sf — CONTRACT

```saffron
import "io" as IO

// Key kinds. Printable runes carry kind KEY_RUNE + the rune in `rune`.
let KEY_RUNE: Int = 0
let KEY_ENTER: Int = 1
let KEY_TAB: Int = 2
let KEY_BACKSPACE: Int = 3
let KEY_ESC: Int = 4
let KEY_UP: Int = 5
let KEY_DOWN: Int = 6
let KEY_LEFT: Int = 7
let KEY_RIGHT: Int = 8
let KEY_HOME: Int = 9
let KEY_END: Int = 10
let KEY_PGUP: Int = 11
let KEY_PGDN: Int = 12
let KEY_DELETE: Int = 13
let KEY_INSERT: Int = 14
let KEY_SPACE: Int = 15
// F1..F12 = 16..27
// modifiers as bitflags in `mods`:
let MOD_NONE: Int = 0
let MOD_CTRL: Int = 1
let MOD_ALT: Int = 2
let MOD_SHIFT: Int = 4

class KeyEvent {
    var kind: Int
    var rune: String     // set when kind == KEY_RUNE
    var mods: Int
    fun init(kind: Int, rune: String, mods: Int)
    // Canonical name for keymap matching, LOWERCASE (matches built input.sf):
    // "ctrl+c", "up", "enter", "a", "f1", "esc", "pgup", "pgdown", "space".
    fun name(): String
}

// Mouse: kind 0=press 1=release 2=motion 3=wheel_up 4=wheel_down.
class MouseEvent {
    var kind: Int
    var button: Int      // 0=left 1=middle 2=right
    var x: Int           // 0-based column
    var y: Int           // 0-based row
    var mods: Int
    fun init(kind: Int, button: Int, x: Int, y: Int, mods: Int)
}

// A parsed event is exactly one of these; the others are nil.
class Event {
    var key: KeyEvent        // nil if not a key event
    var mouse: MouseEvent    // nil if not a mouse event
    var paste: String        // non-nil (may be "") if this is a paste event
    var resize: Bool         // true if this is a resize marker (runtime injects)
    var focus: Int           // -1 none, 0 blur, 1 focus
    fun init(...)
}

// Parse a raw input byte range into zero or more events. Stateless per call is
// fine for v1 (a single read() usually holds whole sequences); return every
// event decoded from `bytes`. Unrecognized escapes are dropped, not errored.
fun parse(bytes: IO.Bytes): List<Event>
```

## layout.sf — CONTRACT

```saffron
import "./buffer.sf" as Buf
import "./style.sf" as St

// The retained node. Built by the builder functions via a context stack
// exactly like Turmeric's _el/_push_ctx/_pop_ctx (copy that mechanism).
enum LNode {
    TextNode(content: String, style: St.Style),
    BoxNode(props: BoxProps, children: List<LNode>)   // vstack/hstack/box
}

// BoxProps carries direction, sizing, borders, padding, alignment, style.
class BoxProps {
    var dir: Int          // 0 = vertical (vstack), 1 = horizontal (hstack)
    var border: Int       // 0 none,1 normal,2 rounded,3 thick,4 double
    var pad_t: Int  var pad_r: Int  var pad_b: Int  var pad_l: Int
    var align: Int        // cross-axis: 0 start 1 center 2 end
    var justify: Int      // main-axis: 0 start 1 center 2 end 3 between
    var width: Int        // -1 = auto/flex, >=0 = fixed
    var height: Int
    var grow: Int         // flex factor, 0 = don't grow
    var style: St.Style   // bg fill + border color
    var title: String     // optional border title
    // ...
}

// Builders (context-stack based). Each pushes a node, runs block, pops, adds
// itself to the current parent (or becomes root), AND RETURNS the LNode (like
// Turmeric's _el). Callers may ignore the return.
fun text(content: String): LNode
fun text_styled(content: String, st: St.Style): LNode   // styled text variant
fun vstack(block: () => Nil): LNode
fun hstack(block: () => Nil): LNode
fun box(...props..., block: () => Nil): LNode            // all fields defaulted
fun box_styled(...style+title..., block: () => Nil): LNode
fun padding(all: Int, block: () => Nil): LNode
fun spacer(): LNode                              // flexible gap (grow=1)

// Context-stack entry points the runtime uses:
fun _push_root(): LayoutCtx
fun _collect(block: () => Nil): List<LNode>      // run block, return children
fun _emit(node: LNode): LNode                    // append to current ctx

// Measure (natural size). measure(node) -> List<Int> [w,h]; measure_w adds a
// width hint so TextNode can report its wrapped height.
fun measure(node: LNode): List<Int>
fun measure_w(node: LNode, avail_w: Int): List<Int>

// Layout + paint: measure the tree, position into (x,y,w,h), rasterize into buf.
fun render_node(node: LNode, buf: Buf.Buffer, x: Int, y: Int, w: Int, h: Int)

// place/overlay: paint `node` over `buf` at an absolute region (for modals).
fun place(node: LNode, buf: Buf.Buffer, x: Int, y: Int, w: Int, h: Int)
fun place_centered(node: LNode, buf: Buf.Buffer, w: Int, h: Int)
```

## widgets — CONTRACT

Each widget lives in its OWN FILE: `sumac/src/widgets/<name>.sf`. A barrel
`sumac/src/widgets.sf` re-exports them (named imports). Widgets depend on
`layout.sf` (emit LNodes), `buffer.sf`, `style.sf`, and `signal.sf`.

**Uniform widget convention.** Every widget is a class that:
- Holds the SIGNALS it binds to (app code creates the signals and passes them in,
  so state ownership stays with the app and reactivity works).
- Has `fun view()` — emits its LNode subtree via the layout builders (called
  inside the app's `view()`; reads signals so the runtime re-renders on change).
- Has `fun handle(e: Ev.Event): Bool` — the runtime feeds it input when focused;
  returns true if it consumed the event (so the runtime can stop propagation).
  Import events as `import "../input.sf" as Ev` and match on `e.key != nil` etc.
- Optional `fun focusable(): Bool` (default true) and `fun blur()/focus()` hooks.

Signals are from the reactive module: `import "../signal.sf" as Sig`, type
`Sig.Signal<T>`, read `.get()`, write `.set()/.update()`.

Example shape (list widget):
```saffron
class List_ {
    var items: Sig.Signal<List<String>>
    var selected: Sig.Signal<Int>
    var height: Int
    fun init(items: Sig.Signal<List<String>>, selected: Sig.Signal<Int>, height: Int) { ... }
    fun view() { /* box { vstack { for each visible row: text_styled(...) } } */ }
    fun handle(e: Ev.Event): Bool { /* up/down move selection; return consumed */ }
}
fun list_widget(items: Sig.Signal<List<String>>, selected: Sig.Signal<Int>, height: Int): List_ { ... }
```

**Target set (one file each):**
- `list.sf` — scrollable selectable list, optional filter string signal
- `viewport.sf` — scrollable content region (offset signal), for long text
- `textinput.sf` — single-line editable field (value signal + cursor), placeholder
- `textarea.sf` — multi-line editable field
- `spinner.sf` — animated spinner (frame signal; `.tick()` advances; several styles)
- `progress.sf` — progress bar 0.0..1.0, block/ramp fill, optional percent label
- `table.sf` — columns + rows, selected-row signal, header styling
- `tabs.sf` — tab bar + active-index signal
- `paginator.sf` — page dots / "N/M" indicator, page signal
- `help.sf` — renders a keymap (list of {key,desc}) as aligned hint text

Each widget's test lives in `sumac/test/test_<name>.sf`. Because BUGS #192 (GC +
cross-module List<class>) is open, widget tests may `import "@gc" as GC` and call
`GC.disable()` at the top (document it); remove once #192 lands.

## runtime.sf — CONTRACT

```saffron
// The entry point. `view` is called to (re)build the tree; it reads signals.
// The runtime: enter() tty, install SIGWINCH, loop { read input → parse →
// dispatch to on_key/on_mouse handlers and focused widget → if dirty, rebuild
// view → paint back buffer → diff vs front → tty.write → swap }, leave() on quit.
fun run(view: () => Nil)

// Register global handlers (analog of Turmeric's _tc_callbacks):
fun on_key(name: String, handler: () => Nil)     // e.g. on_key("ctrl+c", quit)
fun on_mouse(handler: (MouseEvent) => Nil)
fun on_event(handler: (Event) => Nil)            // catch-all
fun quit()                                       // break the loop, restore tty

// Request a redraw (usually automatic via effects, but exposed for timers):
fun request_render()
```
