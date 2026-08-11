# Styling & Color

Sumac's style layer (`style.sf`) is the terminal analog of Lip Gloss. It is a
pure leaf module: a `Style` is an immutable value — a foreground color, a
background color, and a bitfield of text attributes — that knows how to serialize
itself to the SGR (Select Graphic Rendition) escape parameters a terminal
understands. Everything on this page is from the **built** `style.sf`, so the
signatures are exact.

## Colors

A `Color` has a `kind` that selects which fields matter:

| `kind` | Meaning | Fields used |
|--------|---------|-------------|
| `0` | terminal default fg/bg (emits no SGR) | — |
| `1` | ANSI 16 | `idx` 0..15 |
| `2` | ANSI 256 | `idx` 0..255 |
| `3` | truecolor | `r`, `g`, `b` each 0..255 |

Construct colors with the free functions:

```saffron
import { color_default, ansi, ansi256, rgb, hex } from "sumac/style"

color_default()          // the terminal's own color; no SGR emitted
ansi(1)                  // ANSI-16 red
ansi256(200)             // 256-palette index 200
rgb(224, 165, 74)        // 24-bit truecolor
hex("#E0A54A")           // truecolor from CSS hex ("#rrggbb" or "#rgb")
```

`hex` accepts an optional leading `#` and both the 6-digit and 3-digit shorthand
forms (`"#f00"` == `"#ff0000"`). Malformed input degrades to black rather than
throwing.

`Color.sgr(fg: Bool)` emits the bare SGR parameter run for a color as a
foreground (`true`) or background (`false`):

```
truecolor fg -> "38;2;R;G;B"     truecolor bg -> "48;2;R;G;B"
ansi256   fg -> "38;5;N"         ansi256   bg -> "48;5;N"
ansi16    fg -> "3N" / "9N"      ansi16    bg -> "4N" / "10N"   (bright = 8..15)
default      -> ""               (contributes nothing)
```

## Attributes

Text attributes are a bitfield. OR the flags together to combine them; test
membership with `(attrs & ATTR_X) != 0`.

```saffron
import {
    ATTR_NONE, ATTR_BOLD, ATTR_FAINT, ATTR_ITALIC,
    ATTR_UNDERLINE, ATTR_BLINK, ATTR_REVERSE, ATTR_STRIKE
} from "sumac/style"
```

| Constant | Value |
|----------|-------|
| `ATTR_NONE` | 0 |
| `ATTR_BOLD` | 1 |
| `ATTR_FAINT` | 2 |
| `ATTR_ITALIC` | 4 |
| `ATTR_UNDERLINE` | 8 |
| `ATTR_BLINK` | 16 |
| `ATTR_REVERSE` | 32 |
| `ATTR_STRIKE` | 64 |

## Style

A `Style` bundles fg, bg, and attributes. It is **immutable**: the fluent
builders each return a *new* `Style`, so a base style can be shared and
specialized without side effects.

```saffron
import { style, rgb, hex } from "sumac/style"

var base = style()                       // default fg/bg, no attributes

var heading = base
    .with_fg(hex("#E0A54A"))             // saffron
    .bold()

var dim_note = base
    .with_fg(rgb(138, 128, 114))
    .italic()

var selected = base
    .with_bg(ansi256(237))
    .with_fg(hex("#E8DFD0"))
```

Fluent builders (each returns a new `Style`):

| Method | Effect |
|--------|--------|
| `with_fg(c: Color)` | set foreground |
| `with_bg(c: Color)` | set background |
| `bold()` | add `ATTR_BOLD` |
| `faint()` | add `ATTR_FAINT` |
| `italic()` | add `ATTR_ITALIC` |
| `underline()` | add `ATTR_UNDERLINE` |
| `blink()` | add `ATTR_BLINK` |
| `reverse()` | add `ATTR_REVERSE` |
| `strike()` | add `ATTR_STRIKE` |

`Style.sgr()` produces the full SGR string that *sets* the style from a clean
baseline — it always leads with the reset parameter (`0`), so each string is
self-contained and order-independent, e.g. `"\x1b[0;1;38;2;255;0;0m"` for bold
truecolor red. `Style.eq(other)` is structural equality (same colors and
attributes) and is what the buffer diff uses to decide whether a cell's style
actually changed.

## Adaptive colors

Pick between a light- and dark-background variant. Unlike Lip Gloss, `style.sf`
stays a pure leaf and takes the decision as a parameter rather than probing the
terminal itself:

```saffron
import { adaptive, hex } from "sumac/style"

var fg = adaptive(hex("#1D1A16"), hex("#E8DFD0"), is_dark)
// is_dark == true  -> the dark variant
// is_dark == false -> the light variant
```

## Capability downsampling

A terminal may support fewer color kinds than a `Style` asks for. `downsample`
maps a color down to the richest kind the terminal supports (`max_kind`, using
the same 0..3 scale as `Color.kind`), approximating with the nearest available
color:

```saffron
import { downsample, rgb } from "sumac/style"

var c = rgb(224, 165, 74)

downsample(c, 3)   // truecolor supported -> unchanged
downsample(c, 2)   // -> nearest ANSI-256 (cube / grayscale ramp)
downsample(c, 1)   // -> nearest of the 16 standard ANSI colors
downsample(c, 0)   // monochrome -> terminal default
```

The mappings:

- **truecolor → 256** — quantize each channel to the xterm 6×6×6 cube or the
  24-step grayscale ramp, whichever is closer.
- **truecolor → 16** — nearest of the 16 standard ANSI colors by squared RGB
  distance.
- **256 → 16** — expand the palette index to RGB, then nearest-16.
- **anything → 0** — collapse to the terminal default.

A color already within `max_kind` is returned unchanged. "Nearest" is plain
squared-Euclidean RGB distance — cheap, and good enough for a TUI.

This is the mechanism behind graceful degradation: design in truecolor, provide
`ansi256()` fallbacks for exactness where you care, and let `downsample` handle
16-color terminals. The hierarchy survives on weight (bold/faint) even when
color collapses entirely.
</content>
