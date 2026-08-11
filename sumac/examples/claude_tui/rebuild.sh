#!/usr/bin/env bash
# Rebuild the claude_tui example to the stable path that `sclaude` symlinks to.
# After this, just run `sclaude` (or ./sclaude-bin) to get the latest build.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
"$repo/tools/saffron" build "$here/src/main.sf" -o "$here/sclaude-bin"
echo "built: $here/sclaude-bin  (run: sclaude)"
