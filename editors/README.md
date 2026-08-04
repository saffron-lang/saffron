# Saffron editor integration

Language support for Saffron in VS Code and IntelliJ, both driven by one
Language Server. Distribution is **source-based** — you build and sideload; there
is no marketplace listing.

## Layout

```
editors/
├── shared/     LSP server (TypeScript). Both editors launch this.
│   ├── src/server.ts      the server: diagnostics, hover, go-to-def,
│   │                      document symbols, completion
│   ├── src/builtins.ts    signatures for implicitly-available modules
│   │                      (IO, Math, …) — GENERATED, see below
│   └── tools/gen-builtins.mjs   regenerates builtins.ts from src/lib/*.sf
├── vscode/     VS Code extension (thin client that starts the shared server)
├── intellij/   IntelliJ plugin (LSP client + TextMate grammar bundle)
└── build.sh    builds all of the above
```

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
- **symbols** are the file's declarations (plus enum variants and class methods),
  each with a name-span, powering the outline, hover, go-to-definition and
  completion.

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
