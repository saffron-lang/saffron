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
#
# The TypeScript builds use each package's LOCAL typescript devDependency, never
# a global `tsc` (there isn't one on a stock machine) — so `npm install` runs
# first in any package missing node_modules.
set -euo pipefail

EDITORS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SKIP_INTELLIJ=0
REGEN_BUILTINS=0
for arg in "$@"; do
  case "$arg" in
    --skip-intellij) SKIP_INTELLIJ=1 ;;
    --regen-builtins) REGEN_BUILTINS=1 ;;
    -h|--help)
      sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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

# 2. VS Code extension.
build_ts "$EDITORS_DIR/vscode"

# 3. IntelliJ plugin (Gradle). Skipped without a JDK or on --skip-intellij.
if [[ "$SKIP_INTELLIJ" == "1" ]]; then
  log "skipping IntelliJ plugin (--skip-intellij)"
elif ! command -v java >/dev/null && [[ -z "${JAVA_HOME:-}" ]]; then
  log "skipping IntelliJ plugin (no java / JAVA_HOME — pass --skip-intellij to silence)"
else
  log "gradle buildPlugin (intellij)"
  (cd "$EDITORS_DIR/intellij" && ./gradlew --console=plain buildPlugin)
fi

log "done"
echo "  LSP server:      editors/shared/out/server.js"
echo "  VS Code out:     editors/vscode/out/extension.js"
if [[ "$SKIP_INTELLIJ" != "1" ]]; then
  echo "  IntelliJ plugin: editors/intellij/build/distributions/*.zip"
fi
