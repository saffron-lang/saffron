# Widgets

Widgets are builder functions that emit `LNode` subtrees and read signals for
their reactive state. Each widget owns its state as a class holding signals, with
a `view()` method (or builder fn) that emits nodes and a `handle(e: Event)`
method the runtime calls to feed it input.

> **API (from spec).** `widgets.sf` is still being built. The widget set and its
> shape follow `sumac/CONTRACT.md`. Constructor argument lists and method names
> below are the specified surface; treat exact signatures as provisional until
> the module lands.

## The widget contract

You create the signals a widget binds to and pass them in, so app code owns the
state:

```saffron
var selected = signal(0)
var lw = ListWidget(items, selected)

fun view() {
    lw.view()          // emits the widget's LNode subtree
}

// The runtime routes input to the focused widget:
//   lw.handle(event)
```

Because the binding signals are yours, you can read them elsewhere, derive
`computed` values from them, or set them from a key handler — the widget stays in
sync automatically. Exactly one widget holds focus at a time; the runtime routes
`KeyEvent`s to that widget's `handle`, while global keys go through `on_key`.

## The gallery

| Widget | Binds to | One-liner |
|--------|----------|-----------|
| `list` | items, selected signal, filter | scrollable, selectable list of items |
| `viewport` | scroll-offset signal | a scrollable window over taller content |
| `textinput` | value signal, cursor | single-line editable text field |
| `textarea` | value signal, cursor | multi-line editable text field |
| `spinner` | frame signal + tick | animated activity indicator |
| `progress` | 0..1 value | a progress bar |
| `table` | columns / rows / selected | tabular data with a selected row |
| `tabs` | active-tab signal | a row of switchable tab headers |
| `paginator` | page signal | page-dots / "n of m" pagination control |
| `help` | keymap | renders a keymap as hint text |

### list

A scrollable, selectable list. Binds to your items and a `selected` signal;
`handle` moves the selection on `up`/`down` and may support a live filter.

```saffron
var items = signal(["one", "two", "three"])
var sel = signal(0)
var lw = ListWidget(items, sel)

fun view() { lw.view() }
```

### viewport

A scrollable window over content taller than the available rows. Holds an
`offset` signal; scroll indicators derive purely from offset, content height, and
viewport height. Ideal for a transcript or log pane that auto-sticks to the
bottom while new content streams.

```saffron
var offset = signal(0)
var vp = Viewport(offset)
```

### textinput

A single-line editable field bound to a `value` signal with a cursor position.
`handle` inserts printable keys, moves the cursor, and applies backspace/delete.
Commonly used as a filter atop a `list`.

```saffron
var query = signal("")
var input = TextInput(query)
```

### textarea

The multi-line sibling of `textinput` — a `value` signal plus a cursor, with
newline insertion (e.g. `shift+enter`) and internal scrolling. This is the
natural home for a paste event's text.

### spinner

An activity indicator driven by a `frame` signal that the runtime advances on a
tick (via `request_render` on a cadence). Swap the glyph set for different moods
(fine Braille dots for "thinking", heavier blocks for "working").

```saffron
var frame = signal(0)
var sp = Spinner(frame)
// a timer calls request_render() ~12fps; sp advances one frame per tick
```

### progress

A bar for a `0..1` value — determinate (block ramp `████░░░░`) or a compact dot
form (`●●●○○`) for tight spaces. Recolor thresholds are up to you (e.g. amber at
80%, red at 95%).

### table

Columns, rows, and a selected-row signal. `handle` moves the selection; render
aligns columns to their widths.

### tabs

A row of tab headers bound to an active-tab signal; `handle` cycles the active
tab. Pair with a `paginator` when the tab content overflows.

### paginator

A pagination control (page dots or "n of m") bound to a page signal — for
splitting long content or a wide `tabs` set across pages.

### help

Renders a keymap into hint text — the footer "`↑↓ scroll  ⏎ send  ^C quit`" line.
Feed it the active keymap; swap keymaps by mode (e.g. idle vs. streaming) with a
`mode` signal.

## Composing widgets

Widgets are just builders, so they nest inside layout like anything else. A
command-palette modal, for instance, is a `box` containing a `textinput` filter, a
`list` bound to the filtered items, and a `help` footer — then `place`d centered
over the dimmed screen:

```saffron
fun palette_view() {
    box(border=4, title="command palette") {   // double border for an overlay
        vstack {
            filter.view()      // textinput
            cmd_list.view()    // list, filtered live by the filter's value signal
            palette_help.view()
        }
    }
}
// composited with place(palette_node, buf, x, y, w, h)
```

See the flagship `sumac/examples/claude_tui/DESIGN.md` for a full application
built entirely from these widgets.
</content>
