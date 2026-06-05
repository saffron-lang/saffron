# CSS in Turmeric Apps

## Context

Turmeric is design-system agnostic. It provides `cls="..."` for class names and
`styles(...)` for inline styles, but ships no CSS opinions. This document
describes the recommended pattern for Turmeric apps that want to use external
CSS tooling (Tailwind, hand-written CSS, Sass, etc.) in production.

## Recommendation: `prebuild` / `postbuild` hooks in pantry.toml

The build tool (`turmeric-build`) already reads `pantry.toml`. We add support
for lifecycle scripts that run before/after compilation:

```toml
[scripts]
prebuild = "tailwindcss -i src/styles.css -o public/styles.css --minify --content 'src/**/*.sf'"
build = "saffron build --target wasm32 --lib-path .pantry/packages src/main.sf -o build/app.wasm"
```

### How it works

1. `turmeric-build` reads `pantry.toml`
2. If `[scripts].prebuild` exists, run it via `Process.run()` before compilation
3. Compile WASM as normal
4. Bundle `public/` into `build/` (copies the CSS output along with everything else)
5. If `[scripts].postbuild` exists, run it after bundling

### Why this approach

- **Framework-agnostic** — works for Tailwind, PostCSS, Sass, Lightning CSS, or
  plain hand-written CSS (no prebuild needed for the latter)
- **No coupling** — Turmeric doesn't know or care what tool produced the CSS
- **Composable** — users chain tools: `sass && tailwindcss && lightningcss`
- **Convention over configuration** — `public/` is already the static asset dir

### Binary distribution

The CSS toolchain binary (e.g. `tailwindcss`) is the user's responsibility:

- Install via system package manager (`brew install tailwindcss`)
- Download standalone binary from the tool's releases
- Future: `pantry` could manage tool binaries (like npm's optionalDependencies)

### Tailwind v4 example

```css
/* src/styles.css */
@import "tailwindcss";

@theme {
  --color-saffron-400: oklch(70% .14 72);
  --color-saffron-500: oklch(65% .14 72);
  --color-navy-900: oklch(11% .01 68);
  --color-navy-950: oklch(10% .01 68);
}
```

```toml
[scripts]
prebuild = "tailwindcss -i src/styles.css -o public/styles.css --minify --content 'src/**/*.sf'"
```

### Plain CSS (no build step)

Just put a stylesheet in `public/`. It gets copied to `build/` during bundling.
No prebuild needed.

---

## Bazaar Redesign

Bazaar uses the "plain CSS" approach: a hand-crafted stylesheet with CSS custom
properties (OKLCH color space). No Tailwind in production.

### Design system

| Token | Value | Usage |
|-------|-------|-------|
| `--bg` | `oklch(13% .012 68)` | Page background |
| `--sf` | `oklch(17% .014 68)` | Surface (cards) |
| `--sfh` | `oklch(21% .015 68)` | Surface hover |
| `--br` | `oklch(26% .012 68)` | Borders |
| `--ac` | `oklch(70% .14 72)` | Accent (saffron gold) |
| `--ink` | `oklch(87% .008 75)` | Primary text |
| `--ink2` | `oklch(63% .008 75)` | Secondary text |
| `--ink3` | `oklch(44% .008 75)` | Muted text |
| `--nav` | `oklch(10% .01 68)` | Nav background |
| `--code` | `oklch(16% .022 68)` | Code blocks |

### Typography

- **Display**: DM Serif Display (italic) — headings, brand
- **Body**: DM Sans (optical size 9–40) — UI text
- **Mono**: JetBrains Mono — code, versions, stats

### Pages

Three views, all SPA routes:

1. **Landing** (`/`) — hero with search, stats strip, trending grid, categories
2. **Search** (`/search?q=...`) — nav search bar, filter sidebar, result rows, pagination
3. **Detail** (`/packages/:name`) — package header with tabs, readme + metadata sidebar

### Implementation approach

1. Replace `frontend/public/style.css` with the new design system CSS
2. Remove Tailwind CDN from `index.html`, add Google Fonts links
3. Rewrite `frontend/src/main.sf` components to use semantic class names
   (`.pkg-card`, `.hero`, `.nav`, etc.) instead of utility classes
4. CSS handles all visual styling; Saffron handles structure and interactivity
