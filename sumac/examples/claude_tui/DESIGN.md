# Sumac Showcase — `claude_tui`

> A prettier front-end for the `claude` CLI, built entirely on Sumac primitives.
> This is a **design document**, not code. A later implementation subagent builds
> against it. Every visual element here maps to something in
> `sumac/CONTRACT.md` — nothing is invented.

---

## 1. Design philosophy — "warm terminal, quiet luxury"

The stock `claude` CLI is functional and flat: undifferentiated wrapped text,
no spatial hierarchy, no color system, everything the same weight. It reads like
a log file. This showcase proves a TUI can feel **composed** — like a well-set
page in a book rather than a scroll of stdout.

The mood is **warm, low-contrast, editorial**. Named after the spice, Saffron's
palette leans into deep ember/amber warmth over a near-black charcoal ground —
the opposite of the cold blue-gray that every other dev tool defaults to. It
should feel like reading by lamplight, not staring at a fluorescent panel.

Three principles drive every decision:

1. **Hierarchy through restraint.** In a terminal you have no font sizes and no
   images. The only levers are *color temperature*, *weight* (bold/dim/italic),
   *whitespace*, and *box-drawing*. We spend them deliberately: one accent color,
   used sparingly, does more than five colors used evenly. Most of the screen is
   dim; the eye is pulled to exactly the two or three things that matter.
2. **The conversation is the product.** Chrome (header, footer, borders) recedes
   — dim, thin, cool. Message content advances — brighter, warmer, framed. A
   user should feel the transcript is *paper on a desk*, and the toolbars are the
   desk itself.
3. **Motion earns its place.** The only animation is the spinner during
   thinking/streaming and a two-frame "message settle" when a bubble first
   appears. Everything else is instant. Restraint reads as premium; jitter reads
   as a toy.

The one thing someone remembers: **rounded amber message bubbles glowing on a
warm-black field, with a saffron caret that pulses while Claude thinks.**

---

## 2. Color palette — "Saffron Dusk" (default theme)

A cohesive dark palette. Every role has a truecolor hex (Sumac `hex()` / `rgb()`,
`Color.kind == 3`) and an ANSI-256 fallback index (`ansi256()`, `Color.kind == 2`)
for terminals without truecolor. The 256-index is chosen to sit as close to the
hex as the 6×6×6 cube / grayscale ramp allows.

| Role            | Purpose                                    | Hex        | ANSI-256 | Notes |
|-----------------|--------------------------------------------|------------|----------|-------|
| `bg`            | App background / void behind everything    | `#12100E`  | `233`    | warm near-black, not `#000` |
| `surface`       | Panel/bubble fill one step above bg        | `#1D1A16`  | `235`    | code blocks, input well |
| `surface_hi`    | Selected row / hovered fill                | `#2A2620`  | `237`    | palette selection, list hover |
| `border`        | Idle box-drawing strokes                   | `#3A342B`  | `240`    | dim, recessive |
| `border_focus`  | Border of the focused region              | `#C8873B`  | `173`    | amber, draws the eye |
| `text`          | Primary body text                          | `#E8DFD0`  | `223`    | warm off-white (paper) |
| `muted`         | Timestamps, hints, secondary text          | `#8A8072`  | `244`    | warm gray |
| `faint`         | Scrollback dimming, disabled               | `#5A5348`  | `240`    | barely-there |
| `primary`       | Saffron — brand accent, caret, focus       | `#E0A54A`  | `179`    | the signature color |
| `accent`        | Ember — secondary highlight, links         | `#D2662E`  | `166`    | code strings, active tab |
| `user_msg`      | User speaker label + bubble border         | `#7FB08A`  | `108`    | sage green (cool, calm) |
| `assistant_msg` | Assistant speaker label + accents          | `#E0A54A`  | `179`    | = primary saffron |
| `code_bg`       | Code block fill                            | `#171410`  | `234`    | slightly darker than surface |
| `code_kw`       | Code keywords (`fun`, `for`, `import`)     | `#C8873B`  | `173`    | amber |
| `code_str`      | Code string literals                       | `#8FA876`  | `107`    | olive green |
| `code_num`      | Code numeric literals                      | `#CE9178`  | `173`    | terracotta |
| `code_comment`  | Code comments                              | `#6B6355`  | `242`    | muted, italic |
| `success`       | Confirmations, "done" states               | `#8FA876`  | `107`    | olive |
| `error`         | Errors, interrupt affordance               | `#D96C5F`  | `167`    | warm coral, not pure red |
| `warning`       | Token-budget pressure, truncation          | `#E0A54A`  | `179`    | = saffron |

**Contrast note.** `text` on `bg` is ~11:1 — well past WCAG AA for the primary
reading surface. `muted` on `bg` is ~4.7:1, adequate for secondary labels.
`faint` is intentionally sub-AA; it is *never* used for content the user must
read, only for dimmed scrollback and disabled affordances.

**Alternate themes** (cycled via `/theme`, all built on the same role names so
widgets never hardcode a hex):

- **Ember** — hotter, higher-contrast: bg `#0E0C0B`, primary `#F26B3A`,
  assistant border in ember instead of saffron. For dark rooms.
- **Mono** — no hue, pure warm grayscale ramp (`233 → 250`), accent = bold only.
  A deliberate "this still looks good with zero color" proof for 16-color TTYs.

**16-color degradation.** When `Color.kind` can only be `1` (ANSI 16), the theme
collapses to: bg=default, text=`ansi(7)`, primary=`ansi(3)` (yellow, bold),
user=`ansi(2)`, accent=`ansi(1)`, muted=`ansi(8)` (bright black). Bubbles keep
their box-drawing; the hierarchy survives on weight alone. This is what proves
Sumac's `Style` model degrades gracefully rather than looking broken.

---

## 3. Typography-in-terminal

No fonts, no sizes — hierarchy is built from four physical levers, all available
in Sumac's `Style` (`ATTR_BOLD`, `ATTR_FAINT`, `ATTR_ITALIC`, `ATTR_UNDERLINE`,
`ATTR_REVERSE`) plus color and box-drawing.

**The type scale (simulated with weight + color, not size):**

| "Level" | Realized as | Used for |
|---------|-------------|----------|
| Display / H1 | `bold` + `primary` saffron | app title in header, modal titles |
| H2 / label | `bold` + role color (`user_msg` / `assistant_msg`) | speaker badges `▌ you`, `◈ claude` |
| Body | plain `text`, default weight | message prose — the calm baseline |
| Caption | `faint`/`muted` + non-bold | timestamps, token counts, hints |
| Code | `text` on `code_bg`, per-token colors | fenced code blocks |
| Emphasis | `italic` `muted` | Claude's *thinking* text, quotes |

**Rules that create rhythm:**

- **One bold per region.** Only the speaker label and the header title are bold.
  Body text is never bold — so when Claude emits `**bold**` inline, we render it
  as `primary`-colored (not bold) to avoid competing with structural weight.
- **Box-drawing is the typographic ruler.** Since we can't use font size for
  separation, borders and rules do the work of headings and horizontal rules on
  a page. `rounded` (`╭╮╰╯`) borders = soft content (message bubbles). `normal`
  (`┌┐└┘`) = code (technical, square). `thick` (`┏┓┗┛`) = the *active/urgent*
  state (input while streaming, "esc to interrupt"). `double` (`╔╗╚╝`) = modal
  overlays, which must read as "above" the plane.
- **Speaker glyphs as small-caps substitute.** `▌ you` (left bar) vs `◈ claude`
  (diamond) give each speaker an instantly-scannable sigil — the terminal
  equivalent of an avatar. The bar/diamond are single cells, colored in the
  speaker's role color.
- **Indentation as whitespace hierarchy.** User messages sit inside a rounded
  bubble indented 2 cols from the left rail; assistant messages run at the rail
  with a hanging glyph. This asymmetry (bubble vs open) distinguishes speakers
  even in Mono theme where both borders are gray.
- **Ellipsis and truncation** use `…` (single glyph) never `...`, and overflow
  indicators use `▲`/`▼` triangles so they read as directional, not decorative.

---

## 4. Full-screen mockups (100×34 target; drawn at 96 wide)

Real terminals vary; the app is fully flexbox and reflows. These mockups are
drawn at 96 columns for legibility and are **cell-exact** (every row is the same
width). Wide/ambiguous glyphs (`⏎ ◈ ▌ ⠋ ⣾`) are treated as one cell, matching
`rune_width()`'s job in `buffer.sf`.

### 4a. Main chat view — idle, one exchange rendered

```
╭── saffron · claude ─────────────────────────────── ◇ sonnet-4.6   ⛁ 12.4k / 200k   ⟳ turn 7 ─╮
│  ╭────────────────────────────────────────────────────────────────────────────────────────╮  │
│  │ ▌ you                                                                          09:41:07│  │
│  │ how do i stream a subprocess line-by-line in saffron?                                  │  │
│  ╰────────────────────────────────────────────────────────────────────────────────────────╯  │
│                                                                                              │
│ ◈ claude                                                                           09:41:08  │
│  Open the process with a pipe and read the stdout handle in a loop. Each read                │
│  returns one chunk; split on newlines and yield completed lines:                             │
│                                                                                              │
│  ┌── saffron ───────────────────────────────────────────────────────────────────────────┐    │
│  │ 1  import "@process" as Process                                                      │    │
│  │ 2                                                                                    │    │
│  │ 3  var p = Process.spawn("claude", ["--stream"])                                     │    │
│  │ 4  for (line in p.stdout.lines()) {                                                  │    │
│  │ 5      IO.println(line)                                                              │    │
│  │ 6  }                                                                                 │    │
│  │                                                                                      │    │
│  └──────────────────────────────────────────────────────────────────────── copy ⧉ · sf ─┘    │
│                                                                                              │
│  The handle is buffered, so the loop blocks until a line is ready. Wrap it in a              │
│  Task.spawn if you want to keep the UI responsive while it drains. ▊                         │
│                                                                                              │
│                                                                               ▲ 3 more ↑     │
│                                                                                              │
├──────────────────────────────────────────────────────────────────────────────────────────────┤
│ ╭── message ────────────────────────────────────────────────────────────── ⏎ send · ⇧⏎ nl ─╮ │
│ │ ▏can you show the async version too?█                                                    │ │
│ │                                                                                          │ │
│ ╰──────────────────────────────────────────────────────────────────────────────────────────╯ │
│                                                                                              │
│ ↑↓ scroll   ⏎ send   ^K palette   ^L clear   ^C quit   ? help                                │
╰──────────────────────────────────────────────────────────────────────────────────────────────╯
```

**Color legend for this frame** (what a paint pass assigns):
- Outer frame stroke → `border` (dim). Title `saffron · claude` → `bold primary`.
- Header right cluster → `◇` glyph `accent`, model name `text`, `⛁ 12.4k/200k`
  `muted` (turns `warning` amber past 80% budget), `⟳ turn 7` `muted`.
- User bubble border → `user_msg` sage. `▌ you` → `bold user_msg`. Timestamp
  `09:41:07` → `faint`. Prose → `text`.
- Assistant `◈ claude` → `bold primary`. Prose → `text`.
- Code block border → `border`; `┌ saffron` tag → `muted`; footer `copy ⧉ · sf`
  → `faint` (copy affordance brightens to `accent` on focus). Line numbers →
  `faint`; keywords (`import`, `var`, `for`) → `code_kw`; strings
  (`"@process"`, `"claude"`) → `code_str`; the trailing `▊` is a soft caret only
  present on the last streamed block.
- Scroll indicator `▲ 3 more ↑` → `muted`, right-aligned.
- Input box border → `border_focus` amber (it holds focus by default), title
  `message` → `muted`, tag `⏎ send · ⇧⏎ nl` → `faint`. `▏` leading bar +
  block cursor `█` → `primary`. Placeholder (when empty) → `faint` italic.
- Footer keys `^K`/`⏎` → `accent`; labels → `muted`.

### 4b. Thinking / streaming state

Header token count ticks up live; a `thinking` panel and a `tool · Read` panel
appear above the incoming reply; the input well switches to a `thick` border and
becomes an **interrupt** affordance. Spinner glyphs advance one frame per tick.

```
╭── saffron · claude ─────────────────────────────── ◇ sonnet-4.6   ⛁ 13.1k / 200k   ⟳ turn 8 ─╮
│  ╭────────────────────────────────────────────────────────────────────────────────────────╮  │
│  │ ▌ you                                                                          09:41:55│  │
│  │ can you show the async version too?                                                    │  │
│  ╰────────────────────────────────────────────────────────────────────────────────────────╯  │
│                                                                                              │
│ ◈ claude                                                                          09:41:56   │
│                                                                                              │
│  ╭── thinking ──────────────────────────────────────────────────────────────────────────╮    │
│  │ ⠋  reasoning about pipe buffering and Task scheduling…   ▒▒▒▒▒▒▒░░░░░░  1.4s         │    │
│  ╰──────────────────────────────────────────────────────────────────────────────────────╯    │
│                                                                                              │
│  ╭── tool · Read ───────────────────────────────────────────────────────────────────────╮    │
│  │ ▸ src/lib/process.sf                                                                 │    │
│  │   ⣾ reading 240 lines…                                            ●●●○○  running     │    │
│  ╰──────────────────────────────────────────────────────────────────────────────────────╯    │
│                                                                                              │
│  Here's the same loop wrapped in a task so the read never blocks your                        │
│  event loop. Async.await collects the result when the stream closes:                         │
│                                                                                              │
│  ┌── saffron ───────────────────────────────────────────────────────────────────────────┐    │
│  │ 1  var task = Task.spawn(fun () => {                                                 │    │
│  │ 2      for (line in p.stdout.lines()) render(line)                                   │    │
│  │ 3  })                                                                                │    │
│  │ █                                                                                    │    │
│  └──────────────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                              │
├──────────────────────────────────────────────────────────────────────────────────────────────┤
│ ┏━━ message ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ esc to interrupt ━┓ │
│ ┃ ▏                                                                                        ┃ │
│ ┃ Claude is responding — press esc to stop                                                 ┃ │
│ ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛ │
│ ⠿ streaming   esc interrupt   ↑↓ scroll   ^C quit                                            │
╰──────────────────────────────────────────────────────────────────────────────────────────────╯
```

**Streaming-specific coloring:**
- `thinking` panel border → `border` dim; title `thinking` → `muted italic`;
  the Braille spinner `⠋` → `primary`; the italic reasoning text → `muted italic`;
  the `▒▒▒▒▒▒▒░░░░░░` ramp is a `progress` widget rendered with `▒` (filled,
  `primary`) vs `░` (track, `faint`); elapsed `1.4s` → `faint`.
- `tool · Read` panel → title `tool · Read` where `tool` is `muted` and `Read`
  is `accent`; the `▸` path glyph → `accent`, path → `text`; the `⣾` spinner →
  `primary`; `●●●○○` dots are a compact `progress` (filled `success`, empty
  `faint`); `running` → `warning`. On completion the panel border flashes
  `success` for one frame then collapses to a single dim line `✓ Read
  src/lib/process.sf (240 lines)`.
- The `█` block caret at the end of the streaming code block is the live insertion
  point, `primary`, blinking via `ATTR_BLINK` (or a two-frame swap if blink is
  disabled).
- Input well border → `border_focus`→`thick` and recolored `error`-adjacent
  `warning`; text is `muted italic`; the whole well is read-only until streaming
  ends. `esc to interrupt` tag → `error`.
- Footer swaps to the streaming keymap; `⠿` spinner → `primary`, `esc interrupt`
  label → `error`.

### 4c. Slash-command palette (modal overlay via `place`)

Triggered by `^K` or by typing `/` at the start of an empty input. The
background transcript is dimmed to `faint` (drawn as a `░` wash here to signal
the dimming; in the real app it's the live tree repainted with every fg forced
to `faint`), and a `double`-bordered modal is `place`d dead-center. The list is a
`list` widget with a live `textinput` filter at the top.

```
╭── saffron · claude ──────────────────────────────────────────────────────────────────────────╮
│                                                                                              │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░╔══ command palette ══════════════════════════════════════ ^K ═╗░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║                                                              ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║ ▏slash…  █                                                   ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░╟──────────────────────────────────────────────────────────────╢░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║ ▸ /model      switch model  ·  sonnet-4.6 → opus / haiku   ⏎ ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║   /clear      reset the conversation transcript              ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║   /compact    summarize + shrink context window              ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║   /copy       yank last assistant reply to clipboard         ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║   /theme      cycle saffron · ember · mono themes            ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║   /export     write transcript to markdown                   ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║   /resume     reattach to a prior claude session             ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║                                                              ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║                                                              ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║                                                              ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║                                                              ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░╟──────────────────────────────────────────────────────────────╢░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░║ ↑↓ navigate   ⏎ run   esc close   7 commands                 ║░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░╚══════════════════════════════════════════════════════════════╝░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ ↑↓ scroll   ⏎ send   ^K palette   ^C quit   ? help                                           │
╰──────────────────────────────────────────────────────────────────────────────────────────────╯
```

**Palette coloring:**
- Wash `░` → `faint` over `bg` (the dimmed live transcript).
- Modal border → `border_focus` amber, `double` style; title `command palette`
  → `bold primary`; `^K` tag → `muted`.
- Filter row: `▏` + `█` caret → `primary`; placeholder `slash…` → `faint italic`.
- Divider rules `╟─╢` → `border`.
- Selected row: full-width `surface_hi` background bar; `▸` marker + command name
  `/model` → `bold primary`; description → `text`; trailing `⏎` hint → `accent`.
- Unselected rows: command name → `accent`, description → `muted`.
- Footer rule + hints → `muted`, count `7 commands` → `faint`.

---

## 5. Component breakdown — screen region → Sumac primitive

Everything below cites only `CONTRACT.md` primitives (`vstack`/`hstack`/`box`,
`text`, `spacer`, `place`, and the named widgets).

| Region | Sumac construction |
|--------|--------------------|
| **Root** | `vstack` filling the terminal (`width -1`, `grow`), `border 2` (rounded), `style` bg=`bg`, border=`border`, `title "saffron · claude"`. |
| **Header bar** | Top child: an `hstack`, `height 1`, `justify 3` (space-between). Left = `text` title (already in the frame title). Right = `hstack` of `text` runs: `◇ model`, `⛁ tokens`, `⟳ turn N`. Token counter recolors to `warning` when `used/max > 0.8`. Rebuilt reactively from a `signal` per counter. |
| **Transcript pane** | The big middle child: a **`viewport`** (scroll offset signal) with `grow 1`. Its content is a `vstack` of **message blocks** built by iterating the messages `signal`. Auto-sticks to bottom while the newest message streams (offset follows content height); manual scroll detaches the stick. |
| **User message** | `box` `border 2` (rounded), `style` border=`user_msg`, `pad_l/r 1`, indented via a leading `spacer`-padded `hstack` or `pad_l 2` on the parent. Header row = `hstack justify 3`: `text "▌ you"` (bold user_msg) + `text timestamp` (faint). Body = wrapped `text`. |
| **Assistant message** | `vstack`, no border (open, runs at the rail). Header `hstack justify 3`: `text "◈ claude"` (bold primary) + `text timestamp` (faint). Body = a `vstack` of segments (prose `text` runs and code blocks) produced by the markdown segmenter (§6). |
| **Code block** | `box` `border 1` (normal), `style` bg=`code_bg`, border=`border`, `title "saffron"` (or detected lang), and a bottom-edge tag `copy ⧉ · sf` (drawn by writing into the border title region — same mechanism `BoxProps.title` uses, mirrored to the bottom row in the example's own paint helper). Inside: a `vstack` of `text` lines, each pre-split into styled runs by the tokenizer (§6) — line-number run (`faint`) + colored token runs. |
| **Thinking panel** | `box` `border 2` rounded, `title "thinking"`. Row = `hstack`: **`spinner`** widget (frame signal + tick) + `text` italic reasoning + a compact **`progress`** (elapsed-vs-estimate or indeterminate ramp) + `text` elapsed. Present only while a `thinking` signal is set. |
| **Tool-call panel** | `box` rounded, `title "tool · <name>"`. Rows: target `text`, then `hstack` of `spinner` + status `text` + `progress` dots. On completion, collapses to a one-line `text` (`✓ …`). One panel per active tool call, stacked. |
| **Scroll indicator** | A `text` line inside the viewport footer area, right-aligned (`hstack justify 2`), showing `▲ N more ↑` / `▼ N more ↓` depending on offset; hidden when fully scrolled. Driven by viewport offset signal + content height. |
| **Divider** | A single `text` row of `─` spanning full width, `border` color, between transcript and input. (Rendered as `├──┤` join by the frame; in a plain child it's a full-width `text`.) |
| **Input area** | `box` `border 2` rounded (→ `thick` while streaming), `border_focus` color when focused, `title "message"`, tag `⏎ send · ⇧⏎ nl`. Contains a **`textarea`** (value signal, cursor) with placeholder. Grows 1–6 rows with content, then scrolls internally. |
| **Footer / hints** | Bottom child, `height 1`: the **`help`** widget rendering the active keymap. Two keymaps (idle / streaming) selected by a `mode` signal. Keys in `accent`, labels in `muted`. |
| **Command palette** | Built as an overlay: a `vstack` modal (`box` `border 4` double, `title "command palette"`) containing a **`textinput`** (filter) + a `list` widget (items filtered live by the input value) + a footer `text`. Composited with **`place`** at a centered `(x,y,w,h)` over the dimmed root. The dim is a full-frame repaint pass that forces every fg to `faint` before `place` draws the modal on top. |
| **Model switcher / settings** | Same modal pattern (`place` + `box` double + `list`). Could also be a **`tabs`** widget inside the modal (General · Model · Theme · Keys) with a `paginator` if a tab overflows. |
| **Toast / status flash** | On `/copy`, `/export`, interrupt, etc.: a small `box` rounded `place`d at bottom-right for ~1.5s (`success` or `error` border), driven by a `toast` signal cleared by a timer + `request_render()`. |

**Focus model in the tree:** exactly one widget holds focus (a `focus` signal
holding a widget id). The runtime routes `KeyEvent`s to the focused widget's
`handle(e)`; global keys (`^K`, `^C`, `^L`, `?`) are registered via
`on_key(...)`. The focused region's `box` uses `border_focus`; all others use
`border`.

---

## 6. Interaction model

### Keybindings

**Idle keymap** (shown in footer, and expanded by `?` into the `help` widget's
full view):

| Key | Action |
|-----|--------|
| `⏎` | Send the current input to `claude` |
| `⇧⏎` | Insert a newline in the input (multi-line prompt) |
| `↑` / `↓` | Scroll transcript one line (when input empty); else move cursor |
| `PgUp` / `PgDn` | Scroll transcript one page |
| `Home` / `End` | Jump transcript to top / bottom (re-sticks to live) |
| `^K` | Open command palette |
| `^L` | Clear transcript (with a confirm toast) |
| `^Y` | Copy last assistant reply (`/copy`) |
| `^R` | Resume / reattach a prior session |
| `?` | Toggle full help overlay |
| `^C` | Quit (double-tap within 1s to confirm; first tap shows a toast) |
| `esc` | Close overlay / clear input if overlay closed |
| `/` | At empty input start → opens palette pre-filtered |

**Streaming keymap** (mode signal flips the footer):

| Key | Action |
|-----|--------|
| `esc` | Interrupt the current generation (send SIGINT to the `claude` child) |
| `↑`/`↓`/`PgUp`/`PgDn` | Scroll (does **not** detach auto-stick unless you scroll up) |
| `^C` | Quit |

**Palette keymap** (when the modal owns focus):

| Key | Action |
|-----|--------|
| type | Filter commands live |
| `↑`/`↓` | Move selection |
| `⏎` | Run selected command |
| `esc` | Close palette |

### Focus flow

- Boot → focus is the **input `textarea`** (amber border). This is the 95% case.
- `↑` with an **empty** input transfers focus intent to the **transcript
  viewport** for scrolling; the first printable key returns focus to the input.
  (No visible focus ring shuffle — the input border stays amber; the viewport
  just scrolls.) This "input-first, scroll-borrows" model avoids a Tab dance for
  the common flow.
- `Tab` explicitly cycles focus input → viewport → input (for keyboard-only
  users who want an explicit ring; the focused region takes `border_focus`).
- Opening any overlay (`^K`, `?`) pushes focus to the overlay; `esc` pops it back
  to the previous focus. A simple focus **stack** in a signal.

### Scroll behavior

- The transcript **`viewport`** holds an `offset` signal. While a reply streams,
  offset auto-tracks the bottom (content grows, offset grows) so new tokens are
  always visible — the classic "tail -f" stick.
- If the user scrolls **up** during streaming, auto-stick **detaches** (a
  `stuck` signal flips false) and a `▼ N more ↓ · jump to latest (End)` hint
  appears bottom-right. Pressing `End` (or `↓` to the bottom) re-attaches.
- Scroll indicators (`▲ N more ↑` / `▼ N more ↓`) derive purely from
  `offset`, `content_height`, and `viewport_height` — no extra state.

### How streaming renders

The `claude` child is spawned via `@process` with piped stdout. A `Task.spawn`
drains `p.stdout.lines()` in the background so the UI event loop never blocks
(exactly the pattern the mockup's own code sample demonstrates — the example is
self-documenting). Each drained chunk:

1. is appended to the in-progress assistant message's text `signal`;
2. the markdown segmenter re-parses that message into prose/code segments
   (cheap: only the tail message is re-segmented, prior messages are frozen);
3. setting the signal marks the frame dirty; the runtime rebuilds the LNode tree,
   repaints the back buffer, and `diff()`s against the front buffer — so only the
   newly-changed cells hit the tty. Streaming is therefore flicker-free even
   though we conceptually "redraw everything."
4. A `spinner` tick timer calls `request_render()` on a fixed cadence
   (~12 fps) so the thinking/streaming spinners animate independently of token
   arrival.

**Markdown segmentation (§6 helper, app-level, not framework):** a small
line-classifier splits an assistant message into segments — fenced code
(```lang … ```), inline `code`, `**bold**`/`*italic*`, `#` headings, `-`/`1.`
lists, and prose. Code segments feed a **toy tokenizer** (keyword set, string
`"…"`, number, comment `//`) that emits per-token styled `text` runs using the
`code_*` roles. This is deliberately *approximate* ("syntax-ish") — enough to
make code blocks read structured without a real parser. Unknown languages fall
back to `text` on `code_bg` with just line numbers.

---

## 7. Micro-details that sell it

- **Caret.** Input caret is a full block `█` in `primary` saffron. When Claude is
  *thinking*, the caret at the assistant insertion point does a slow two-frame
  swap `█`↔`▊` (~2fps) — a subtle "breathing" pulse that reads as *alive*, not
  blinking-cursor-annoying. Achieved by a `spinner`-style frame signal, not
  `ATTR_BLINK`, so it's consistent across terminals.
- **Message settle animation.** When a new bubble first appears, it renders for
  one frame with a `faint` border, then snaps to full color on the next frame —
  a 1-frame "ink settling" that makes messages feel *placed* rather than
  *appended*. Nearly subliminal; costs one extra dirty frame.
- **Spinner vocabulary.** Two distinct spinners so the user can tell states
  apart at a glance: Braille dots `⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏` for *thinking* (fine, cerebral)
  and the heavier `⣾⣽⣻⢿⡿⣟⣯⣷` for *tool work* (chunky, mechanical). Footer uses
  `⠿` (all-dots) as a steady "busy" bug.
- **Progress ramps.** Determinate progress uses a `█`→`░` block ramp
  (`████████░░░░`) in `primary`/`faint`; the compact dot form `●●●○○` is for
  tight spaces (tool panels). Indeterminate uses a scanning `▒` band sliding
  through a `░` track.
- **Timestamps** are `faint`, right-aligned, `HH:MM:SS`, and only shown on the
  message *header* row — never per-line — to keep the body clean. Relative age
  ("2m ago") could replace absolute time via a `/theme`-adjacent toggle.
- **Selection highlight.** In the palette/list, the selected row is a full-width
  `surface_hi` background bar (via a `box` child with `style` bg set and no
  border) plus a `▸` marker and bold label — not `ATTR_REVERSE`, which looks
  cheap. Reverse video is reserved for one thing: a visual-select mode over
  transcript text (future), where `ATTR_REVERSE` genuinely fits.
- **Token budget bar.** The header `⛁ 12.4k / 200k` is backed by a hairline
  `progress` that could render as a 10-cell micro-bar `▕████░░░░░░▏` that turns
  `warning` at 80% and `error` at 95% — a persistent, glanceable context gauge.
- **Copy affordance.** The `copy ⧉ · sf` tag on code blocks brightens from
  `faint` to `accent` when its block is the focused/selected one; `^Y` (or
  clicking it, via `MouseEvent`) copies and fires a `✓ copied` toast.
- **Rounded everything soft, square everything technical.** The consistent
  grammar — rounded `╭╮╰╯` for human/conversational surfaces, square `┌┐└┘` for
  code, thick `┏┓┗┛` for active/urgent, double `╔╗╚╝` for overlays — means the
  user learns to read *border style as a category* within seconds.
- **Empty state.** Before the first message: the transcript pane centers a small
  `vstack` — a saffron `◈` glyph, `text` "ask claude anything" in `muted`, and
  three dim example-prompt chips (`text` in `faint`, `border` rounded) the user
  can `↑`/`↓` to and `⏎` to prefill. Turns a blank screen into an invitation.
- **Resize.** On SIGWINCH the whole tree reflows (flexbox); message wrapping
  recomputes, the viewport preserves its bottom-stick, and the modal re-centers
  via fresh `place` coordinates. The `diff()` full-repaint path (`force`) handles
  the first post-resize frame.

---

## Appendix — mapping to CONTRACT primitives (quick audit)

Nothing in this design requires a primitive Sumac doesn't list:

- **Colors**: all roles are `rgb()`/`hex()` (truecolor) with `ansi256()`
  fallbacks — `Color.kind` 3 and 2. 16-color path uses `ansi()` (kind 1).
- **Styles**: `bold`, `italic`, `faint`, `underline`, `reverse`, `blink` — all in
  the `ATTR_*` set. Fluent `with_fg/with_bg/bold/…` builders assemble them.
- **Layout**: `vstack`/`hstack`/`box` with `dir`, `justify` (incl. `3`=between),
  `align`, `pad_*`, `width/height/grow`, `border` 0–4, `title`, `style` bg+border.
- **Overlays**: `place(node, buf, x, y, w, h)` for palette/settings/toasts.
- **Widgets**: `viewport` (transcript), `textarea` (input), `textinput` (filter),
  `list` (palette), `spinner` ×2, `progress` (thinking/tool/token bars),
  `help` (footer keymap), `tabs`+`paginator` (settings). `table` is unused by the
  core flow but available for a future `/sessions` browser.
- **Input**: `KeyEvent.name()` drives the keymaps; `MouseEvent` powers scroll
  wheel + click-to-copy; paste events fill the textarea; resize marker triggers
  reflow.
- **Runtime**: `run(view)`, `on_key`, `on_mouse`, `on_event`, `quit`,
  `request_render` (spinner cadence + toast expiry timers).

The only *app-level* logic beyond the framework is the `@process` plumbing to the
`claude` child and the markdown segmenter/toy tokenizer in §6 — both pure Saffron,
no new framework surface.
