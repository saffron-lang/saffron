# Architecture

Sumac's rendering model is a **retained tree** that mirrors Turmeric. You write
builder functions; they assemble a tree of layout nodes; a layout pass positions
them; a paint pass rasterizes them into an off-screen cell grid; and a diff turns
that grid into the minimal stream of ANSI bytes needed to update the terminal.

## The pipeline

```
        your view() function
               │  reads signals, calls builders
               ▼
  builders push LNodes onto a context stack        layout.sf / widgets.sf
               │  (block runs → children collected → pop)
               ▼
        ┌──────────────┐
        │  LNode tree  │   TextNode | BoxNode(props, children)
        └──────┬───────┘
               │  layout pass: measure each node, position into (x, y, w, h)
               ▼
        paint pass: rasterize styled runes into a back Buffer   buffer.sf
               │
               ▼
   ┌───────────────────────────┐     ┌───────────────────────────┐
   │  back Buffer (just built) │     │  front Buffer (on screen) │
   └─────────────┬─────────────┘     └─────────────┬─────────────┘
                 └──────────  diff(front, back)  ───┘
                               │  minimal cursor-moves + styled runs
                               ▼
                   ANSI byte stream → tty.write()               tty.sf → tty_native.c
                               │
                               ▼
                        copy back → front   (swap)
```

The reactive runtime closes the loop: when a signal changes, it re-runs `view()`,
repaints the back buffer, diffs against the front buffer, flushes the diff, and
swaps. The simplest-correct v1 marks the whole frame dirty on any change — the
diff keeps it flicker-free regardless, because unchanged cells produce no output.

## Four ideas reused from Turmeric

Sumac is deliberately the same shape as the web framework, so knowing one
teaches the other.

1. **Context-stack builders.** Builder functions don't return nodes to be
   wired up by hand; each one *pushes* a node onto an implicit context stack,
   runs its trailing-closure block (during which child builders attach to it),
   then pops. This is exactly Turmeric's `_el` / `_push_ctx` / `_pop_ctx`
   mechanism — `layout.sf` copies it. The entry points the runtime uses are
   `_push_root()` and `_collect(block)` (run a block, return the children it
   produced).

2. **Signals.** `signal`, `computed`, and `effect` are the same primitives, with
   automatic dependency tracking: reading `signal.get()` inside a tracked context
   records a subscription, and setting the signal notifies subscribers. Sumac
   ships its own `signal.sf` (identical model to Turmeric's) so a TUI has the
   full reactive toolkit — see [Reactivity](./reactivity.md).

3. **The reconciler concept.** Turmeric reconciles a retained element tree against
   the DOM and issues surgical DOM ops. Sumac reconciles a retained **LNode**
   tree against the terminal — but because a terminal is a flat grid, not a node
   graph, the "reconcile" step is the buffer **diff**: it compares the freshly
   painted grid to the grid currently on screen and emits only the changed cells.
   Same idea (retain, re-derive, apply the minimum), different substrate.

4. **Declarative views as functions of state.** In both frameworks your UI is a
   function you write once; the framework decides when to call it and what to
   update. You never imperatively mutate the screen.

## The one terminal-specific layer

Everything above is portable reactivity. The piece that has no web analog is the
**cell buffer + tty layer**:

- **`buffer.sf`** — a `Buffer` is a flat, row-major grid of `Cell`s (a rune, a
  `Style`, and a display width). The paint pass writes styled runes into it;
  `diff(front, back, force)` walks the back buffer against the front buffer and
  returns the minimal ANSI stream (cursor moves + styled runs) that transforms
  one into the other. It is wide-character aware: CJK/emoji occupy two columns,
  with a width-0 placeholder cell for the trailing half, so the grid stays
  column-aligned. `force` ignores the front buffer and repaints everything —
  used on the first frame and after a resize.

- **`tty.sf`** — a thin Saffron skin over `src/runtime/tty_native.c`. It owns raw
  mode, the alternate screen, cursor hide/show, mouse + bracketed-paste + focus
  reporting, the terminal size query, and the low-level input read. `enter()`
  puts the terminal into full-screen UI mode and `leave()` reverses every step;
  they are idempotent and must be paired so a crash never leaves the user in a
  dead, echo-less terminal.

The style layer (`style.sf`) sits underneath both: a `Style` is an immutable
value (fg color, bg color, attribute bitfield) that knows how to serialize itself
to SGR escape parameters. Colors downsample automatically to whatever the
terminal supports. See [Styling & Color](./styling.md).

## Module dependency graph

```
tty.sf        (leaf — native primitives + ANSI constants)
style.sf      (leaf — Color, Attr, Style)
buffer.sf     → style.sf
input.sf      → io.Bytes
signal.sf     (leaf — reactive primitives)
layout.sf     → buffer.sf + style.sf
widgets.sf    → layout.sf + buffer.sf + style.sf + signal.sf
runtime.sf    → all of the above + tty.sf + input.sf + signal.sf
```

## A note on integer discipline

Saffron numbers are `Float` unless annotated `Int`, and a `Float`-typed list
index silently reads element 0 (BUGS #72). The rendering core is entirely
list-indexed — buffer cells, palette tables, byte positions — so every
coordinate, width, byte value, and color component is annotated `Int`.
Coordinates are **0-based** inside the buffer; the 1-based ANSI cursor move is
produced inside `diff` (row+1, col+1). When you write your own paint-adjacent
helpers, keep indices `Int`.
</content>
