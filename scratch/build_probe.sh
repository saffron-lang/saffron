#!/usr/bin/env bash
# Stage-1-only build into a scratch dir, so build/saffronc is left alone.
# Usage: scratch/build_probe.sh <outdir>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMPILER_DIR="$ROOT/src/compiler"
OUT="${1:?outdir}"
mkdir -p "$OUT/stage3"
GEN2="$ROOT/build/stage2/saffronc"

sed -e "/@codegen-split: types/r $COMPILER_DIR/codegen/types_body.sf" \
    -e "/@codegen-split: expr/r $COMPILER_DIR/codegen/expr_body.sf" \
    -e "/@codegen-split: match/r $COMPILER_DIR/codegen/match_body.sf" \
    -e "/@codegen-split: closures/r $COMPILER_DIR/codegen/closures_body.sf" \
    -e "/@codegen-split: intrinsics/r $COMPILER_DIR/codegen/intrinsics_body.sf" \
    -e "/@codegen-split: stmts/r $COMPILER_DIR/codegen/stmts_body.sf" \
    -e "/@codegen-split: utils/r $COMPILER_DIR/codegen/utils_body.sf" \
    -e "/@codegen-split: output/r $COMPILER_DIR/codegen/output_body.sf" \
    -e "/@codegen-split: methods/r $COMPILER_DIR/codegen/methods_body.sf" \
    "$COMPILER_DIR/codegen.sf" > "$OUT/stage3/_codegen.sf"

cp "$COMPILER_DIR/main.sf" "$OUT/stage3/_main.sf"
for f in lexer parser checker resolve ast; do cp "$COMPILER_DIR/$f.sf" "$OUT/stage3/$f.sf"; done

for src in lexer parser; do
  "$GEN2" --identity-mode --stdlib "$ROOT/src/lib" "$COMPILER_DIR/$src.sf" "$OUT/stage3/${src}.ll"
done
"$GEN2" --identity-mode --stdlib "$ROOT/src/lib" "$OUT/stage3/_codegen.sf" "$OUT/stage3/codegen.ll"
sed -i '' 's|import "./codegen.sf" as Codegen|import "./_codegen.sf" as Codegen|' "$OUT/stage3/_main.sf"
"$GEN2" --identity-mode --stdlib "$ROOT/src/lib" "$OUT/stage3/_main.sf" "$OUT/stage3/main.ll"
"$GEN2" --identity-mode --stdlib "$ROOT/src/lib" "$ROOT/src/runtime/runtime.sf" "$OUT/stage3/runtime.ll"

clang -O2 -w -Wl,-stack_size,0x10000000 -o "$OUT/saffronc" \
  "$OUT/stage3/main.ll" "$OUT/stage3/runtime.ll" \
  "$ROOT/src/runtime/base.ll" "$ROOT/src/runtime/gc.ll"
echo "built $OUT/saffronc"
