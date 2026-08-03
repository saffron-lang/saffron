#!/usr/bin/env bash
# Sweep the corpus with a probe compiler, collecting MPMISS lines.
# Usage: scratch/sweep_probe.sh <compiler> <outdir>
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${1:?compiler}"
OUT="${2:?outdir}"
mkdir -p "$OUT/ir"
: > "$OUT/misses.txt"
: > "$OUT/all.txt"
n=0
for f in "$ROOT"/test/*.sf "$ROOT"/test/pass/*.sf "$ROOT"/test/fail/*.sf \
         "$ROOT"/src/lib/*.sf "$ROOT"/examples/*.sf "$ROOT"/examples/**/*.sf \
         "$ROOT"/src/compiler/*.sf "$ROOT"/src/runtime/runtime.sf \
         "$ROOT"/turmeric/**/*.sf "$ROOT"/basil/**/*.sf "$ROOT"/bazaar/**/*.sf \
         "$ROOT"/parsley/**/*.sf "$ROOT"/pantry/**/*.sf; do
  [ -f "$f" ] || continue
  n=$((n+1))
  base="$(echo "$f" | sed "s|$ROOT/||; s|/|_|g; s|\.sf$||")"
  log="$OUT/ir/${base}.log"
  # NEVER put .sf in the output name (BUGS #119)
  timeout 120 "$CC" --stdlib "$ROOT/src/lib" "$f" "$OUT/ir/${base}.ll" > "$log" 2>&1
  rc=$?
  echo "$rc $f" >> "$OUT/all.txt"
  if grep -q MPMISS "$log"; then
    grep MPMISS "$log" | sed "s|^|$f :: |" >> "$OUT/misses.txt"
  fi
done
echo "swept $n files"
echo "--- miss lines: $(wc -l < "$OUT/misses.txt") ---"
sort -u "$OUT/misses.txt" | head -80
