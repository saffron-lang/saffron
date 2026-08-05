#!/usr/bin/env bash
# build.sh — build every editor integration from a clean checkout.
#
# What it builds, in order:
#   1. editors/shared   — the LSP server (TypeScript -> out/server.js). Both
#                         editors launch THIS file, so it must build first.
#   2. editors/vscode   — the VS Code extension (TypeScript -> out/extension.js).
#   3. editors/intellij — the IntelliJ plugin (Gradle -> build/distributions/*.zip),
#                         unless --skip-intellij is passed or no JDK is found.
#
# Everything it produces (out/, build/, node_modules/) is gitignored.
#
# Usage:
#   editors/build.sh                 # build shared + vscode + intellij
#   editors/build.sh --skip-intellij # skip the Gradle build (no JDK needed)
#   editors/build.sh --regen-builtins# regenerate builtins.ts from the stdlib first
#   editors/build.sh --test          # also run the shared server's unit tests
#   editors/build.sh --install       # also install to ~/.saffron (see below)
#
# --install is what makes the plugins work on a project OUTSIDE this repo, which
# is the normal way anyone uses them. The IntelliJ plugin looks for the server at
# $PROJECT/editors/shared/out/server.js and then at ~/.saffron/lsp/server.js, so
# without the second location a non-Saffron-repo project has no server at all.
# Getting it right needs three things that are each easy to miss:
#   - node_modules alongside server.js. It requires vscode-languageserver at
#     runtime, so copying the .js files alone fails with "Cannot find module".
#   - the compiler at ~/.saffron/bin/saffronc (findCompiler's fallback).
#   - src/lib at ~/.saffron/src/lib. `@`-prefixed imports resolve relative to the
#     EXECUTABLE, so a compiler installed without the stdlib beside it rejects
#     every `import "@test"` with "cannot resolve import".
#
# The TypeScript builds use each package's LOCAL typescript devDependency, never
# a global `tsc` (there isn't one on a stock machine) — so `npm install` runs
# first in any package missing node_modules.
set -euo pipefail

EDITORS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SKIP_INTELLIJ=0
REGEN_BUILTINS=0
RUN_TESTS=0
DO_INSTALL=0
for arg in "$@"; do
  case "$arg" in
    --skip-intellij) SKIP_INTELLIJ=1 ;;
    --regen-builtins) REGEN_BUILTINS=1 ;;
    --test) RUN_TESTS=1 ;;
    --install) DO_INSTALL=1 ;;
    -h|--help)
      sed -n '2,34p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) echo "build.sh: unknown argument: $arg" >&2; exit 2 ;;
  esac
done

log() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }

# Run `npm install` only when node_modules is absent, so re-runs are fast.
ensure_deps() {
  local dir="$1"
  if [[ ! -d "$dir/node_modules" ]]; then
    log "npm install ($(basename "$dir"))"
    (cd "$dir" && npm install --no-audit --no-fund)
  fi
}

# Build a TypeScript package with its own local tsc.
build_ts() {
  local dir="$1"
  ensure_deps "$dir"
  local tsc="$dir/node_modules/.bin/tsc"
  if [[ ! -x "$tsc" ]]; then
    echo "build.sh: no local tsc in $dir after npm install" >&2
    exit 1
  fi
  log "tsc ($(basename "$dir"))"
  (cd "$dir" && "$tsc")
}

command -v node >/dev/null || { echo "build.sh: node not found on PATH" >&2; exit 1; }
command -v npm  >/dev/null || { echo "build.sh: npm not found on PATH" >&2; exit 1; }

# 0. Optionally regenerate builtins.ts from the stdlib before the shared build.
if [[ "$REGEN_BUILTINS" == "1" ]]; then
  log "regenerate builtins.ts from src/lib"
  node "$EDITORS_DIR/shared/tools/gen-builtins.mjs"
fi

# 1. shared LSP server — the file both editors run.
build_ts "$EDITORS_DIR/shared"

# 1b. The shared server's tests: the lexical scanner that drives rename (whose
# failures are silent edits rather than crashes) and the server itself, driven
# over stdio against build/saffronc. Opt-in rather than always-on so a plain
# build stays a build, and because the server suite shells out to the compiler
# once per document. It skips itself, loudly, when there is no usable compiler.
if [[ "$RUN_TESTS" == "1" ]]; then
  log "node --test (shared)"
  (cd "$EDITORS_DIR/shared" && node --test "test/*.test.mjs")
fi

# 2. VS Code extension.
build_ts "$EDITORS_DIR/vscode"

# 2b. The client's server-discovery tests. They load the COMPILED out/extension.js
# with `vscode` stubbed, so they must run after the tsc step, not beside the shared
# suite. What they guard is the one client failure that is invisible in the editor:
# a LanguageClient pointed at a server module that does not exist starts, reports
# nothing, and leaves every file looking clean.
if [[ "$RUN_TESTS" == "1" ]]; then
  log "node --test (vscode)"
  (cd "$EDITORS_DIR/vscode" && node --test "test/*.test.mjs")
fi

# 3. IntelliJ plugin (Gradle). Skipped without a JDK or on --skip-intellij.
if [[ "$SKIP_INTELLIJ" == "1" ]]; then
  log "skipping IntelliJ plugin (--skip-intellij)"
elif ! command -v java >/dev/null && [[ -z "${JAVA_HOME:-}" ]]; then
  log "skipping IntelliJ plugin (no java / JAVA_HOME — pass --skip-intellij to silence)"
else
  log "gradle buildPlugin (intellij)"
  (cd "$EDITORS_DIR/intellij" && ./gradlew --console=plain buildPlugin)

  # --test also runs the JetBrains Plugin Verifier, which is the only check that
  # sees the failures compilation cannot: API that does not exist at the
  # sinceBuild floor. It caught a PluginId.getId() call that compiled fine and
  # would have thrown NoSuchFieldError on any IDE older than 2025.3. Off by
  # default because it downloads a full IDE per verified version (GBs, slow).
  if [[ "$RUN_TESTS" == "1" ]]; then
    log "gradle verifyPlugin (intellij)"
    (cd "$EDITORS_DIR/intellij" && ./gradlew --console=plain verifyPlugin)
  fi
fi

# 4. Optionally install to ~/.saffron so the plugins work outside this repo.
#
# Writes only under ~/.saffron, and only paths this project owns. It does NOT
# touch PATH, shell profiles, or any editor configuration.
if [[ "$DO_INSTALL" == "1" ]]; then
  ROOT="$(cd "$EDITORS_DIR/.." && pwd)"
  PREFIX="${SAFFRON_PREFIX:-$HOME/.saffron}"
  log "install to $PREFIX"

  mkdir -p "$PREFIX/lsp" "$PREFIX/bin" "$PREFIX/src"
  cp "$EDITORS_DIR/shared/out/"*.js "$PREFIX/lsp/"
  cp "$EDITORS_DIR/shared/package.json" "$PREFIX/lsp/"
  # -R over a fresh copy: node_modules is large, and rsync is not guaranteed.
  rm -rf "$PREFIX/lsp/node_modules"
  cp -R "$EDITORS_DIR/shared/node_modules" "$PREFIX/lsp/node_modules"
  echo "  server:  $PREFIX/lsp/server.js"

  # The compiler and the stdlib travel TOGETHER or the install is broken in a way
  # that only shows up on files using `@` imports — see the header note.
  if [[ -x "$ROOT/build/saffronc" ]]; then
    cp "$ROOT/build/saffronc" "$PREFIX/bin/saffronc"
    rm -rf "$PREFIX/src/lib"
    cp -R "$ROOT/src/lib" "$PREFIX/src/lib"
    echo "  compiler: $PREFIX/bin/saffronc (with stdlib at $PREFIX/src/lib)"
  else
    echo "  compiler: SKIPPED — no build/saffronc; run ./bootstrap.sh, then re-run with --install" >&2
  fi

  # Verify rather than assume: start the installed server from a directory with
  # no relation to this repo and check it loads its dependencies. A silent bad
  # install presents in the editor as "no diagnostics", which is indistinguishable
  # from clean code — so it has to be checked here or not at all.
  #
  # A clean start produces NO output and exits 0 on EOF. The check is therefore on
  # the output being empty, not on the status: `set -e` is in force, so the
  # command runs under `if` to keep a nonzero exit from aborting the script.
  if [[ -x "$PREFIX/bin/saffronc" ]]; then
    startup_out=""
    if ! startup_out=$(cd / && node "$PREFIX/lsp/server.js" --stdio </dev/null 2>&1); then
      : # nonzero exit is reported through the output check below
    fi
    if [[ -n "$startup_out" ]]; then
      echo "  WARNING: the installed server printed on startup — it is not usable:" >&2
      echo "$startup_out" | head -5 >&2
    else
      echo "  verified: the installed server starts cleanly outside the repo"
    fi
  fi
fi

log "done"
echo "  LSP server:      editors/shared/out/server.js"
echo "  VS Code out:     editors/vscode/out/extension.js"
if [[ "$SKIP_INTELLIJ" != "1" ]]; then
  echo "  IntelliJ plugin: editors/intellij/build/distributions/*.zip"
fi
