#!/usr/bin/env bash
# Sweep the corpus, recording exit code and any "no member" diagnostic.
# Usage: scratch/sweep_diag.sh <compiler> <outdir>
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${1:?compiler}"
OUT="${2:?outdir}"
mkdir -p "$OUT/ir"
: > "$OUT/all.txt"
: > "$OUT/diags.txt"
for f in "$ROOT"/test/*.sf "$ROOT"/test/pass/*.sf "$ROOT"/test/fail/*.sf \
         "$ROOT"/src/lib/*.sf "$ROOT"/examples/*.sf "$ROOT"/examples/**/*.sf \
         "$ROOT"/src/compiler/*.sf "$ROOT"/src/runtime/runtime.sf \
         "$ROOT"/turmeric/**/*.sf "$ROOT"/basil/**/*.sf "$ROOT"/bazaar/**/*.sf \
         "$ROOT"/parsley/**/*.sf "$ROOT"/pantry/**/*.sf; do
  [ -f "$f" ] || continue
  base="$(echo "$f" | sed "s|$ROOT/||; s|/|_|g; s|\.sf$||")"
  log="$OUT/ir/${base}.log"
  timeout 120 "$CC" --stdlib "$ROOT/src/lib" "$f" "$OUT/ir/${base}.ll" > "$log" 2>&1
  echo "$? $f" >> "$OUT/all.txt"
  if grep -q "no member" "$log"; then
    grep "no member" "$log" | sed "s|^|$f :: |" >> "$OUT/diags.txt"
  fi
done
echo "=== new diagnostics ==="
sort -u "$OUT/diags.txt"
