#!/usr/bin/env bash
# bootstrap.sh — Bootstrap the Saffron compiler
#
# Normal:  gen2 (checked-in) compiles gen3 from source
# Full:    no longer available — see the --full branch below
#
# Usage: ./bootstrap.sh [--verbose]

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
COMPILER_DIR="$ROOT/src/compiler"
RUNTIME_SRC="$ROOT/src/runtime/runtime.sf"
RUNTIME_BASE="$ROOT/src/runtime/base.ll"
RUNTIME_GC="$ROOT/src/runtime/gc.ll"
BUILD_DIR="$ROOT/build"
GEN2="$BUILD_DIR/stage2/saffronc"

FULL=false
VERBOSE=false

for arg in "$@"; do
    case "$arg" in
        --full) FULL=true ;;
        --verbose) VERBOSE=true ;;
    esac
done

# --- Helpers ---

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { printf "${CYAN}[%s]${NC} %s\n" "$1" "$2"; }
pass()  { printf "${GREEN}[%s]${NC} %s\n" "$1" "$2"; }
fail()  { printf "${RED}[%s]${NC} %s\n" "$1" "$2"; exit 1; }

# --- Preflight ---

if ! command -v clang &>/dev/null; then
    fail "ERROR" "clang not found. Install LLVM/Clang."
fi

mkdir -p "$BUILD_DIR/stage2" "$BUILD_DIR/stage3"

echo ""
echo "╔══════════════════════════════════════╗"
echo "║   Saffron Bootstrap                  ║"
echo "╚══════════════════════════════════════╝"
echo ""

# =============================================================================
# Full rebuild: retired
# =============================================================================
#
# --full used to rebuild gen2 from the C VM. That path is gone. The C VM was
# moved to legacy/ and has drifted far enough behind the language that it can no
# longer parse the compiler's own source — it rejects `///` doc comments outright,
# before it even reaches `Int`, `@` annotations, functor types or actors. So this
# is not a matter of repointing the paths at legacy/: there is nothing there that
# can compile src/compiler/*.sf.
#
# The checked-in gen2 at build/stage2/saffronc is now the sole root of trust for
# the bootstrap chain. Losing it means recovering it from git history, not
# regenerating it. Fail loudly rather than leaving a flag that silently pretends
# to work.

if [[ "$FULL" == true ]]; then
    fail "FULL" "--full is no longer supported: the C VM (now in legacy/) cannot parse the current compiler source. Recover build/stage2/saffronc from git history instead."
fi

# =============================================================================
# Stage 1: gen2 (checked-in) → gen3
# =============================================================================

[[ -x "$GEN2" ]] || fail "STAGE 1" "gen2 not found at $GEN2. Run with --full to rebuild."

info "STAGE 1" "Compiling saffronc via gen2..."

SOURCES=(lexer parser)

# Assemble codegen from parts (insert extensions at markers)
[[ "$VERBOSE" == true ]] && echo "  assemble: codegen"
sed -e "/@codegen-split: types/r $COMPILER_DIR/codegen/types_body.sf" \
    -e "/@codegen-split: expr/r $COMPILER_DIR/codegen/expr_body.sf" \
    -e "/@codegen-split: match/r $COMPILER_DIR/codegen/match_body.sf" \
    -e "/@codegen-split: closures/r $COMPILER_DIR/codegen/closures_body.sf" \
    -e "/@codegen-split: intrinsics/r $COMPILER_DIR/codegen/intrinsics_body.sf" \
    -e "/@codegen-split: stmts/r $COMPILER_DIR/codegen/stmts_body.sf" \
    -e "/@codegen-split: utils/r $COMPILER_DIR/codegen/utils_body.sf" \
    -e "/@codegen-split: output/r $COMPILER_DIR/codegen/output_body.sf" \
    -e "/@codegen-split: methods/r $COMPILER_DIR/codegen/methods_body.sf" \
    "$COMPILER_DIR/codegen.sf" > "$BUILD_DIR/stage3/_codegen.sf"
sed -i '' '/^import "\.\/codegen\/methods\.sf"/d' "$BUILD_DIR/stage3/_codegen.sf"

# Try gen2 first; if it fails (e.g. AST has new variants gen2 doesn't know),
# fall back to linking from checked-in .ll artifacts compiled by gen3.
GEN2_OK=true
for src in "${SOURCES[@]}"; do
    [[ "$VERBOSE" == true ]] && echo "  compile: $src.sf"
    if ! timeout 180 "$GEN2" --identity-mode --stdlib "$ROOT/src/lib" "$COMPILER_DIR/$src.sf" "$BUILD_DIR/stage3/${src}.ll" 2>/dev/null; then
        GEN2_OK=false
        break
    fi
done

if [[ "$GEN2_OK" == true ]]; then
    [[ "$VERBOSE" == true ]] && echo "  compile: codegen.sf (assembled)"
    timeout 180 "$GEN2" --identity-mode --stdlib "$ROOT/src/lib" "$BUILD_DIR/stage3/_codegen.sf" "$BUILD_DIR/stage3/codegen.ll" \
        || GEN2_OK=false
fi

if [[ "$GEN2_OK" == true ]]; then
    # Compile main.sf with a modified copy that imports the assembled codegen
    [[ "$VERBOSE" == true ]] && echo "  compile: main.sf"
    cp "$COMPILER_DIR/main.sf" "$BUILD_DIR/stage3/_main.sf"
    cp "$COMPILER_DIR/lexer.sf" "$BUILD_DIR/stage3/lexer.sf"
    cp "$COMPILER_DIR/parser.sf" "$BUILD_DIR/stage3/parser.sf"
    cp "$COMPILER_DIR/checker.sf" "$BUILD_DIR/stage3/checker.sf"
    cp "$COMPILER_DIR/ast.sf" "$BUILD_DIR/stage3/ast.sf"
    # Rewrite the codegen import to use the assembled file and strip methods import
    sed -i '' 's|import "./codegen.sf" as Codegen|import "./_codegen.sf" as Codegen|' "$BUILD_DIR/stage3/_main.sf"
    sed -i '' '/^import "\.\/codegen\/methods\.sf"/d' "$BUILD_DIR/stage3/_main.sf"
    timeout 180 "$GEN2" --identity-mode --stdlib "$ROOT/src/lib" "$BUILD_DIR/stage3/_main.sf" "$BUILD_DIR/stage3/main.ll" \
        || GEN2_OK=false
fi

if [[ "$GEN2_OK" == true ]]; then
    # Compile runtime.sf
    [[ "$VERBOSE" == true ]] && echo "  compile: runtime.sf"
    timeout 180 "$GEN2" --identity-mode --stdlib "$ROOT/src/lib" "$RUNTIME_SRC" "$BUILD_DIR/stage3/runtime.ll" \
        || fail "STAGE 1" "gen2 failed to compile runtime.sf"
fi

if [[ "$GEN2_OK" == false ]]; then
    # gen2 cannot compile current source (new AST variants, etc.)
    # Link gen3 from checked-in .ll files, then use gen3 to recompile itself
    info "STAGE 1" "gen2 outdated, bootstrapping via checked-in .ll artifacts..."
    clang -O2 -w -Wl,-stack_size,0x10000000 -o "$BUILD_DIR/saffronc" \
        "$BUILD_DIR/stage3/main.ll" \
        "$BUILD_DIR/stage3/runtime.ll" \
        "$RUNTIME_BASE" \
        "$RUNTIME_GC" \
        || fail "STAGE 1" "Linking gen3 from .ll artifacts failed"
    GEN3="$BUILD_DIR/saffronc"

    # Now use gen3 to recompile itself from current source
    for src in "${SOURCES[@]}"; do
        [[ "$VERBOSE" == true ]] && echo "  compile (gen3): $src.sf"
        timeout 180 "$GEN3" --identity-mode --stdlib "$ROOT/src/lib" "$COMPILER_DIR/$src.sf" "$BUILD_DIR/stage3/${src}.ll" \
            || fail "STAGE 1" "gen3 failed to compile $src.sf"
    done

    [[ "$VERBOSE" == true ]] && echo "  compile (gen3): codegen.sf (assembled)"
    timeout 120 "$GEN3" --identity-mode --stdlib "$ROOT/src/lib" "$BUILD_DIR/stage3/_codegen.sf" "$BUILD_DIR/stage3/codegen.ll" \
        || fail "STAGE 1" "gen3 failed to compile codegen.sf"

    cp "$COMPILER_DIR/main.sf" "$BUILD_DIR/stage3/_main.sf"
    cp "$COMPILER_DIR/lexer.sf" "$BUILD_DIR/stage3/lexer.sf"
    cp "$COMPILER_DIR/parser.sf" "$BUILD_DIR/stage3/parser.sf"
    cp "$COMPILER_DIR/checker.sf" "$BUILD_DIR/stage3/checker.sf"
    cp "$COMPILER_DIR/ast.sf" "$BUILD_DIR/stage3/ast.sf"
    sed -i '' 's|import "./codegen.sf" as Codegen|import "./_codegen.sf" as Codegen|' "$BUILD_DIR/stage3/_main.sf"
    sed -i '' '/^import "\.\/codegen\/methods\.sf"/d' "$BUILD_DIR/stage3/_main.sf"
    [[ "$VERBOSE" == true ]] && echo "  compile (gen3): main.sf"
    timeout 120 "$GEN3" --identity-mode --stdlib "$ROOT/src/lib" "$BUILD_DIR/stage3/_main.sf" "$BUILD_DIR/stage3/main.ll" \
        || fail "STAGE 1" "gen3 failed to compile main.sf"

    [[ "$VERBOSE" == true ]] && echo "  compile (gen3): runtime.sf"
    timeout 180 "$GEN3" --identity-mode --stdlib "$ROOT/src/lib" "$RUNTIME_SRC" "$BUILD_DIR/stage3/runtime.ll" \
        || fail "STAGE 1" "gen3 failed to compile runtime.sf"
fi

[[ "$VERBOSE" == true ]] && echo "  linking gen3..."
clang -O2 -w -Wl,-stack_size,0x10000000 -o "$BUILD_DIR/saffronc" \
    "$BUILD_DIR/stage3/main.ll" \
    "$BUILD_DIR/stage3/runtime.ll" \
    "$RUNTIME_BASE" \
    "$RUNTIME_GC" \
    || fail "STAGE 1" "Linking gen3 failed"

pass "STAGE 1" "gen3 saffronc built: $BUILD_DIR/saffronc"
echo ""

# =============================================================================
# Test: Compile and run an example program with gen3
# =============================================================================

info "TEST" "Compiling example program with gen3..."

EXAMPLE="$ROOT/test/hello_bootstrap.sf"

cat > "$EXAMPLE" << 'EOF'
var name = "Saffron"
var version = "0.1.0"
IO.println("Hello from ${name} ${version}!")
IO.println("Bootstrapped successfully.")
IO.println("The compiler compiled itself. We're self-hosting!")
EOF

"$BUILD_DIR/saffronc" "$EXAMPLE" "$BUILD_DIR/hello_bootstrap.ll" \
    || fail "TEST" "gen3 failed to compile example"

clang -O2 -w -o "$BUILD_DIR/hello_bootstrap" "$BUILD_DIR/hello_bootstrap.ll" "$BUILD_DIR/stage3/runtime.ll" "$RUNTIME_BASE" "$RUNTIME_GC" \
    || fail "TEST" "Linking example failed"

echo ""
echo "--- Running compiled example ---"
"$BUILD_DIR/hello_bootstrap"
echo "--- End ---"
echo ""

pass "TEST" "Example compiled and ran successfully!"
echo ""

# =============================================================================
# Summary
# =============================================================================

echo "╔══════════════════════════════════════╗"
echo "║   Bootstrap complete!                ║"
echo "╠══════════════════════════════════════╣"
echo "║   gen3 compiler: build/saffronc      ║"
echo "║   driver:        tools/saffron       ║"
echo "║                                      ║"
echo "║   Usage:                             ║"
echo "║     saffron run program.sf           ║"
echo "║     saffron build program.sf -o app  ║"
echo "╚══════════════════════════════════════╝"
echo ""
