# Layout

Sumac's layout system is a flexbox-ish box model built from three container
builders — `vstack`, `hstack`, and `box` — plus `text`, `spacer`, and `place`.
You assemble a tree with trailing closures; a layout pass measures and positions
each node into a region; the paint pass rasterizes it into the cell buffer.

> **API (from spec).** `layout.sf` is still being built. The types and builders
> on this page follow `sumac/CONTRACT.md`. Exact builder overloads and named-arg
> spellings may shift as the module lands — anything not yet verifiable is flagged
> inline.

## The retained tree

Layout builds a tree of `LNode`s:

```saffron
enum LNode {
    TextNode(content: String, style: Style),
    BoxNode(props: BoxProps, children: List<LNode>)   // vstack / hstack / box
}
```

A `BoxNode` carries its layout via `BoxProps`:

| Field | Meaning |
|-------|---------|
| `dir` | main axis: `0` vertical (vstack), `1` horizontal (hstack) |
| `border` | `0` none, `1` normal `┌┐└┘`, `2` rounded `╭╮╰╯`, `3` thick `┏┓┗┛`, `4` double `╔╗╚╝` |
| `pad_t` / `pad_r` / `pad_b` / `pad_l` | padding per edge |
| `align` | cross-axis: `0` start, `1` center, `2` end |
| `justify` | main-axis: `0` start, `1` center, `2` end, `3` space-between |
| `width` / `height` | `-1` auto/flex, `>= 0` fixed |
| `grow` | flex factor; `0` = don't grow |
| `style` | background fill + border color |
| `title` | optional border title |

## Builders

Builders are context-stack based: each pushes a node, runs its block (during
which child builders attach to it), pops, and adds itself to the current parent
(or becomes the root).

```saffron
import { vstack, hstack, box, text, spacer } from "sumac/layout"

fun view() {
    vstack {
        text("top")
        hstack {
            text("left")
            spacer()            // flexible gap — pushes the next child to the end
            text("right")
        }
        box(border=2, title="panel") {
            text("inside a rounded box")
        }
    }
}
```

- **`text(content: String)`** — a text run. Styled variants take a `Style` (shown
  in these docs as `text_styled(content, style)`; the exact name is **from spec**).
- **`vstack(block)`** — vertical container (children stacked top-to-bottom).
- **`hstack(block)`** — horizontal container (children left-to-right).
- **`box(...props..., block)`** — a container with borders, padding, alignment,
  and a background. Named args map to `BoxProps` fields (`border=`, `title=`,
  `pad_l=`, `align=`, `justify=`, `width=`, `height=`, `grow=`, `style=`). The
  precise named-arg set is **from spec**.
- **`spacer()`** — a flexible gap that expands to fill free space on the main
  axis (the classic flex spacer for pushing siblings apart).

## Sizing and flex

- A child with `grow > 0` expands to absorb leftover space on its parent's main
  axis, proportional to its grow factor.
- `width` / `height` of `-1` means "size to content / flex"; `>= 0` pins a fixed
  cell count.
- `spacer()` is the idiomatic way to push content apart (e.g. a title on the left
  and status on the right in a header `hstack`).

```saffron
// A full-width header: title left, status right.
hstack {
    text("saffron · claude")
    spacer()
    text("turn 7")
}
```

Equivalently, `justify=3` (space-between) on the `hstack` distributes children to
the edges without an explicit spacer.

## Alignment & justification

- **`justify`** controls placement along the main axis (the stacking direction):
  `0` start, `1` center, `2` end, `3` space-between.
- **`align`** controls the cross axis: `0` start, `1` center, `2` end.

```saffron
// Center a message both ways inside a bordered box.
box(border=2, align=1, justify=1) {
    text("centered")
}
```

## Borders, padding, and titles

Five border styles double as a visual grammar (a convention the flagship example
leans on): rounded for soft/conversational surfaces, normal for technical/code,
thick for active/urgent, double for overlays.

```saffron
box(border=2, title="message", pad_l=1, pad_r=1) {   // rounded, padded, titled
    text("hello")
}
```

The `title` is drawn into the top border. Border color and background fill come
from the box's `style` (`BoxProps.style`).

## Rendering

The runtime drives layout + paint for you, but the entry points exist for custom
rendering and overlays:

```saffron
// Layout + paint: measure the tree, position into (x, y, w, h), paint into buf.
render_node(node, buf, x, y, w, h)

// Overlay: paint `node` over an existing buffer at an absolute region.
place(node, buf, x, y, w, h)
```

`place` is how modals, popovers, and toasts are composited: build a subtree, then
`place` it at a computed centered region over the already-painted screen. The
flagship `claude_tui` example uses `place` for its command palette and settings
modal.

## Context-stack entry points

The runtime uses these to collect a frame's tree:

```saffron
_push_root(): LayoutCtx
_collect(block: () => Nil): List<LNode>    // run block, return the children it built
```

These mirror Turmeric's `_push_ctx` / `_pop_ctx` machinery — you rarely call them
directly, but they are why a builder's trailing closure can attach children to
the node above it without explicit wiring.
</content>
