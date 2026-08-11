# Input Handling

Sumac's input layer (`input.sf`) turns a raw stdin byte range — as produced by
`tty.read_input` — into a list of high-level `Event`s: key presses, mouse
actions, bracketed-paste blocks, and focus in/out markers. This page describes
the **built** `input.sf`; the parser and event types are exact. The runtime's
handler-registration functions (`on_key`, `on_mouse`, `on_event`) are **from
spec** (`runtime.sf` is in progress).

## The event model

`parse(bytes)` returns a `List<Event>`. Each `Event` populates exactly one facet;
the rest are the nil/default sentinel:

```saffron
class Event {
    var key: KeyEvent      // non-nil if this is a key event
    var mouse: MouseEvent  // non-nil if this is a mouse event
    var paste: String      // non-nil (may be "") if this is a paste event
    var resize: Bool       // true if this is a resize marker (runtime injects it)
    var focus: Int         // -1 none, 0 blur, 1 focus
}
```

Unrecognized escape sequences are dropped, not errored — a stray byte never
crashes the loop or emits garbage.

## Keys

A `KeyEvent` has a `kind`, an optional `rune` (set when `kind == KEY_RUNE`), and a
`mods` bitfield.

```saffron
class KeyEvent {
    var kind: Int
    var rune: String   // set when kind == KEY_RUNE
    var mods: Int
    fun name(): String
}
```

### Key kinds

Printable characters arrive as `KEY_RUNE` with the decoded grapheme in `rune`.
Every other key is a named kind:

| Kind | Value | | Kind | Value |
|------|-------|-|------|-------|
| `KEY_RUNE` | 0 | | `KEY_END` | 10 |
| `KEY_ENTER` | 1 | | `KEY_PGUP` | 11 |
| `KEY_TAB` | 2 | | `KEY_PGDN` | 12 |
| `KEY_BACKSPACE` | 3 | | `KEY_DELETE` | 13 |
| `KEY_ESC` | 4 | | `KEY_INSERT` | 14 |
| `KEY_UP` | 5 | | `KEY_SPACE` | 15 |
| `KEY_DOWN` | 6 | | `KEY_F1`..`KEY_F12` | 16..27 |
| `KEY_LEFT` | 7 | | | |
| `KEY_RIGHT` | 8 | | | |
| `KEY_HOME` | 9 | | | |

### Modifiers

`mods` is a bitfield: `MOD_NONE` (0), `MOD_CTRL` (1), `MOD_ALT` (2),
`MOD_SHIFT` (4). Test with `(mods & MOD_CTRL) != 0`.

### Canonical names

`KeyEvent.name()` produces the string you match against in a keymap. Modifier
prefixes are applied in **ctrl+alt+shift** order (matching Bubbletea's
convention):

| Key pressed | `name()` |
|-------------|----------|
| `a` | `"a"` |
| Ctrl-C | `"ctrl+c"` |
| Alt-X | `"alt+x"` |
| Up arrow | `"up"` |
| Enter | `"enter"` |
| Space | `"space"` |
| Escape | `"esc"` |
| Page Down | `"pgdown"` |
| F5 | `"f5"` |
| Shift-Tab | `"shift+tab"` |

These are exactly the strings `on_key(name, handler)` matches:

```saffron
on_key("ctrl+c", quit)
on_key("up",     fun () => scroll_up())
on_key("q",      quit)
```

> Note the base names produced by the parser: `esc`, `pgup`, `pgdown`, `space`,
> and function keys as lowercase `f1`..`f12`. Ctrl+letter arrives as a
> `KEY_RUNE` of the letter with `MOD_CTRL`, so it names as `"ctrl+a"`..`"ctrl+z"`.

## Mouse

Mouse events are decoded from SGR mouse sequences (click, drag, motion, wheel):

```saffron
class MouseEvent {
    var kind: Int     // 0 press, 1 release, 2 motion, 3 wheel-up, 4 wheel-down
    var button: Int   // 0 left, 1 middle, 2 right
    var x: Int        // 0-based column
    var y: Int        // 0-based row
    var mods: Int
}
```

Kind constants: `MOUSE_PRESS` (0), `MOUSE_RELEASE` (1), `MOUSE_MOTION` (2),
`MOUSE_WHEEL_UP` (3), `MOUSE_WHEEL_DOWN` (4). Coordinates are converted from the
1-based wire protocol to 0-based buffer coordinates on decode, so `x`/`y` index
straight into a `Buffer`.

```saffron
on_mouse(fun (e) => {
    if (e.kind == 3) { scroll_up() }        // wheel up
    if (e.kind == 4) { scroll_down() }      // wheel down
    if (e.kind == 0 and e.button == 0) {    // left press
        click_at(e.x, e.y)
    }
})
```

## Paste

Bracketed paste (enabled by `tty.enter()`) arrives as a single event with the
pasted text in `paste`. Capturing the whole block as one event — rather than a
storm of synthetic keystrokes — lets you drop a multi-line paste straight into a
`textarea`:

```saffron
on_event(fun (e) => {
    if (e.paste != nil) {
        input_value.set(input_value.get() + e.paste)
    }
})
```

## Focus and resize

- **Focus** — `e.focus` is `1` on focus-in, `0` on blur, `-1` when the event is
  not a focus event. Focus reporting is enabled by `tty.enter()`.
- **Resize** — `e.resize` is `true` for the resize marker the runtime injects on
  SIGWINCH. The tree reflows automatically (the first post-resize frame uses the
  diff's full-repaint `force` path); hook `on_event` if you need to run extra work
  on a resize.

```saffron
on_event(fun (e) => {
    if (e.resize)   { /* size changed; layout already reflows */ }
    if (e.focus == 0) { pause_animations() }
    if (e.focus == 1) { resume_animations() }
})
```

## Parsing directly

If you drive input yourself (e.g. in a test), call `parse` on a byte range read
from the tty:

```saffron
import "sumac/tty" as Tty
import { parse } from "sumac/input"

var bytes = Tty.read_input(64, -1)     // up to 64 bytes, block for the first
var events = parse(bytes)              // List<Event>
```

A single `read` often carries several sequences (and a whole paste payload)
back-to-back; `parse` decodes every event it finds and is stateless per call.
</content>
