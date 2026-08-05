# Saffron editor integration

Language support for Saffron in VS Code and IntelliJ, both driven by one
Language Server. Distribution is **source-based** — you build and sideload; there
is no marketplace listing.

## Layout

```
editors/
├── shared/     LSP server (TypeScript). Both editors launch this.
│   ├── src/server.ts      the server: every LSP request handler
│   ├── src/scan.ts        the lexical identifier scanner (pure; unit-tested)
│   ├── src/builtins.ts    signatures for implicitly-available modules
│   │                      (IO, Math, …) — GENERATED, see below
│   ├── test/scan.test.mjs      unit tests for the scanner (no compiler needed)
│   ├── test/server.test.mjs    end-to-end LSP tests (needs build/saffronc)
│   └── tools/gen-builtins.mjs   regenerates builtins.ts from src/lib/*.sf
├── vscode/     VS Code extension (thin client that starts the shared server)
├── intellij/   IntelliJ plugin (LSP client + TextMate grammar bundle)
└── build.sh    builds all of the above
```

## Capabilities

| Capability | Source of truth |
|---|---|
| Diagnostics (live, debounced 400ms) | compiler `--json` `diagnostics` |
| Hover — declaration + doc comment | `symbols` (`detail`, `doc`, `visibility`) |
| Go-to-definition (incl. cross-file, stdlib) | `symbols` + import resolution |
| Document symbols — nested outline | `symbols` (`container`, extent) |
| Workspace symbols | `symbols` of open files |
| Completion — signature, doc, accessibility-filtered | `symbols` + `builtins.ts` |
| — where `internal` = nearest `pantry.toml` (package, not directory) | `docs/design/access-modifiers.md` |
| Signature help | `symbols` (`detail`) |
| Folding — declaration bodies + doc-comment runs | `symbols` extent + line scan |
| Semantic tokens | `symbols` + `scan.ts` |
| Find references / document highlight | `scan.ts` (name-based — see below) |
| Rename | `scan.ts`, restricted to locally-declared names |
| Formatting | `saffronc format` |

### What is name-based, and why that matters

References, highlight, rename and semantic tokens match on the **name**, not on
the binding. The compiler resolves variable references (`resolve.sf` produces
`Ref(kind, name, slot)`) but leaves member and method accesses unresolved, and
the `--json` payload carries declarations only — there is no reference list. So a
field `count` on one class and a local `count` in another function are
indistinguishable to the server.

The features handle that differently, on purpose:

- **References and highlight** report the superset. Guessing which occurrences
  belong to the binding under the cursor would silently *omit* real call sites,
  and a find-references that quietly misses matches is worse than one showing a
  few extra the user can see and dismiss.
- **Rename** is restricted instead: `prepareRename` refuses any name the
  compiler did not declare in this file, and refuses outright when the file does
  not compile (the declaration set is then unknown). It is also single-file — a
  name exported and used elsewhere is not followed. A wrong reference list is a
  UI annoyance; a wrong rename is a silent code edit.

The matching is lexical but **not** a regex. `src/scan.ts` knows that `//`, `///`
and `//!` run to end of line, that `/* */` block comments *nest* (as
`lexer.sf` does), that string bodies are not code — and that `${...}` inside a
string **is**, so `"total ${count}"` genuinely references `count` and a rename
must rewrite it.

### Tests

```bash
editors/build.sh --test          # build, then run both suites
cd editors/shared && npm test    # same, without the vscode build
```

Two layers, because they fail differently:

- **`test/scan.test.mjs`** — the scanner and the two pure helpers, with no
  compiler and no LSP connection. Each case is one of the things a regex gets
  wrong (nesting comments, `${...}`, escaped quotes, a qualifier split across a
  line break).
- **`test/server.test.mjs`** — the real server as a subprocess, spoken to over
  stdio JSON-RPC, backed by the real `build/saffronc`. Every handler only runs
  when a client asks for it, so before this existed a handler that threw or
  returned the wrong shape was indistinguishable from one that worked until an
  editor was attached by hand. It asserts the things that are wrong *silently*:
  that each capability flag is actually advertised (an unadvertised handler is
  dead code — the client never calls it), that semantic-token deltas are
  non-negative and in-legend (a negative delta shifts every colour after it),
  that no folding range collapses to one line, and that a type error lands on
  the right column.

  It **skips**, with the reason attached to each test, when `build/saffronc` is
  missing or predates `--json`. Note that `node:test` treats any `skip` value
  other than `false`/`undefined` as a skip — returning `null` for "nothing is
  wrong" silently skipped the whole suite while reporting 14 passes.

## How it works

The server shells out to the Saffron compiler in `--json` mode
(`saffronc --json file.sf /dev/null`) and reads the single JSON object it prints:

```json
{ "file": "...", "diagnostics": [ ... ], "symbols": [ ... ] }
```

- **diagnostics** carry `severity`, `phase`, `message` and — when the compiler
  has a source region — `located: true` with `line`/`column`/`offset`/`length`.
  A diagnostic with `located: false` (many checker errors have no region yet) is
  deliberately **not** placed as a squiggle, rather than pinned to line 0.
- **symbols** are the file's declarations — including enum variants, class
  fields and methods — each carrying:
  - `line`/`column`/`offset`/`length` for the **name** alone (the rename target);
  - `start_offset`/`end_offset` for the declaration's **full extent**, used for
    folding. Absent when the node has no real extent, in which case no folding
    range is invented: one folded line reads as a broken control.
  - `visibility` — always present, defaulting to `public`, so completion can
    filter by accessibility without every consumer guessing the default back;
  - `container` — the owning declaration's name for a member, `""` at top level,
    which is what lets the outline nest (a flat list cannot say which `init`
    belongs to which class, and there are usually several);
  - `detail` — a rendered signature or type, **for display only**. It is
    deliberately not a parseable encoding: the comma-joined `"name:Type,…"` form
    used elsewhere in the compiler corrupts on `Map<String,Int>`. Where the
    server does need parameter boundaries (signature help) it splits on
    *top-level* commas only, ignoring those nested in `<>`, `()` or `[]`.
  - `doc` — the doc comment with the parser's internal markers (`@extend:`,
    `@actor`, `@type_alias`) stripped, so they never surface as prose.

### Formatting

`textDocument/formatting` shells out to `saffronc format` (no `--write`) with the
**open buffer** staged to a temp file, and returns the result as a single
whole-document `TextEdit`. It never writes your file: under format-on-save the
buffer is ahead of disk, so `--write` would format stale text and lose the undo
entry. A compiler too old to know the `format` subcommand yields *no edits*
rather than an emptied buffer — silently leaving text alone is the only safe
failure mode for a formatter. Already-formatted text also returns no edits, so
saving a clean file does not mark it dirty.

The formatter itself (`src/lib/formatter.sf`) only rewrites whitespace between
lexical pieces; comments, `${...}` interpolation and every piece of syntactic
sugar are copied verbatim from the input. `test/pass/formatter_fidelity.sf` is
the regression test, and it asserts the general property (strip all whitespace
from input and output — they must be byte-identical) rather than a list of cases.

This replaced an older path that scraped the compiler's human-readable output
with a regex; that regex only matched the *parser's* error format, so type and
codegen errors never reached the editor. See `src/compiler/diag.sf` for the
producing side.

### Compiler discovery

The server looks for the `saffronc` binary in this order (`findCompiler` in
`server.ts`):

1. `saffron.compilerPath` from the editor settings, if set;
2. `<project>/build/saffronc` (the binary a local `./bootstrap.sh` produces);
3. `$HOME/.saffron/bin/saffronc`;
4. `saffronc` on `PATH`.

> **The compiler must understand `--json`.** That support is recent; a stale
> `build/saffronc` will emit human-readable text and the server will show no
> diagnostics. Rebuild the compiler (`./bootstrap.sh` at the repo root) if the
> editor is silent on a file you know has errors.

## Prerequisites

- **Node.js** 18+ (20+ recommended) and **npm**, for the server and the VS Code
  extension. No global `tsc` is required — each package builds with its own local
  TypeScript devDependency.
- **A JDK 21** and the bundled Gradle wrapper, only for the IntelliJ plugin.
  Skip it with `build.sh --skip-intellij` if you only want VS Code.
- **A built Saffron compiler** at `build/saffronc` (run `./bootstrap.sh` at the
  repo root).

## Build

```bash
editors/build.sh                  # shared + vscode + intellij
editors/build.sh --skip-intellij  # shared + vscode only (no JDK needed)
editors/build.sh --regen-builtins # regenerate builtins.ts from the stdlib first
editors/build.sh --test           # also run the shared server's unit tests
```

The script runs `npm install` in any package missing `node_modules`, then builds
in dependency order (shared first — both editors launch its `out/server.js`).
Outputs (`out/`, `build/`, `node_modules/`) are gitignored.

## Sideloading

### VS Code

The extension launches `../shared/out/server.js`, so build both first
(`build.sh` does). Then either:

- **Run from source:** open `editors/vscode/` in VS Code and press <kbd>F5</kbd>
  ("Run Extension") to launch an Extension Development Host with it loaded; or
- **Package a `.vsix`:** `cd editors/vscode && npx vsce package`, then
  `code --install-extension saffron-lang-*.vsix`.

Set `saffron.compilerPath` in settings if your `saffronc` is not at
`<project>/build/saffronc` or on `PATH`.

### IntelliJ (Ultimate)

`build.sh` produces a plugin zip at
`editors/intellij/build/distributions/*.zip`. Install it via
**Settings → Plugins → ⚙ → Install Plugin from Disk…**, then restart.

The plugin needs Node.js to run the LSP server; it looks for `node` under nvm,
Homebrew, `/usr/local/bin`, then `PATH`, and for the server at
`<project>/editors/shared/out/server.js` (falling back to
`$HOME/.saffron/lsp/server.js`). Its LSP client is available in IntelliJ
**Ultimate** only (the LSP API is not in Community).

For quick iteration, `cd editors/intellij && ./gradlew runIde` launches a
sandbox IDE with the plugin loaded.

## Working on a project outside this repo

Both plugins find the server and the compiler by looking **inside the open
project** first. Open this repo and everything resolves; open a Saffron project
anywhere else and, by default, nothing does — the IntelliJ plugin reports the
server as missing, and even once it starts there is no `saffronc` for it to run,
which surfaces as **no diagnostics at all** rather than as an error.

Install once to fix both:

```bash
editors/build.sh --install     # writes only under ~/.saffron
```

That places three things, and all three are required:

| Path | Why |
|---|---|
| `~/.saffron/lsp/server.js` + `node_modules/` | The plugin's fallback server location. The `.js` files alone are not enough — the server `require`s `vscode-languageserver` at runtime, so without `node_modules` beside it node exits with `Cannot find module`, which the IDE reports as the server crashing. |
| `~/.saffron/bin/saffronc` | `findCompiler`'s fallback, after walking up from the server for a `build/saffronc`. |
| `~/.saffron/src/lib/` | `@`-prefixed imports (`import "@test"`) resolve relative to the **executable**, so a compiler installed without the stdlib next to it rejects every one of them with `cannot resolve import`. |

`--install` verifies the result by starting the installed server from outside the
repo, because a partial install is invisible in the editor: it looks exactly like
a file with no problems. Override the location with `SAFFRON_PREFIX=/some/path`.
Nothing outside that prefix is touched — not `PATH`, not shell profiles, not any
editor configuration.

Re-run it after a bootstrap; the installed compiler is a copy, not a link, so it
does not track `build/saffronc`.

## Regenerating `builtins.ts`

`src/builtins.ts` holds signatures for the modules a program can call without an
import (`IO`, `Math`, `Json`, `Async`, `Reflect`, `Time`, and the natively-
implemented `Task`). Everything except `Task` is generated from the real stdlib
sources so it cannot drift:

```bash
node editors/shared/tools/gen-builtins.mjs           # rewrite in place
node editors/shared/tools/gen-builtins.mjs --check   # fail if stale (for CI)
```

Only the array between the `@generated` markers is rewritten; the exported
helpers around it are hand-maintained. `Task` has no `.sf` source and its entry
is carried across regenerations verbatim.

## Grammar

The TextMate grammar is **one source, two copies** kept byte-identical:
`editors/vscode/syntaxes/saffron.tmLanguage.json` and
`editors/intellij/src/main/resources/textmate/saffron.tmLanguage.json`. Edit the
VS Code copy, then `cp` it to the IntelliJ bundle. The same applies to
`language-configuration.json`.
