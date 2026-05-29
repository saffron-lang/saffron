#!/usr/bin/env bash
# bootstrap.sh — Bootstrap the Saffron compiler
#
# Normal:  gen2 (checked-in) compiles gen3 from source
# Full:    C VM → gen2 → gen3 (rebuilds gen2 from scratch)
#
# Usage: ./bootstrap.sh [--full] [--verbose]

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
VM="$ROOT/cvm/cmake-build-debug/saffron"
COMPILER_DIR="$ROOT/src/compiler"
RUNTIME_SRC="$ROOT/src/runtime/runtime.sf"
RUNTIME_BASE="$ROOT/src/runtime/base.ll"
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
# Full rebuild: C VM → gen2 (only with --full)
# =============================================================================

if [[ "$FULL" == true ]]; then
    info "FULL" "Rebuilding gen2 from C VM..."

    # Build C VM
    cmake -B "$ROOT/cvm/cmake-build-debug" -DCMAKE_BUILD_TYPE=Debug -S "$ROOT/cvm" > /dev/null 2>&1
    cmake --build "$ROOT/cvm/cmake-build-debug" > /dev/null 2>&1
    [[ -x "$VM" ]] || fail "FULL" "Failed to build C VM"

    SOURCES=(lexer parser codegen main)
    for src in "${SOURCES[@]}"; do
        [[ "$VERBOSE" == true ]] && echo "  compile: $src.sf"
        "$VM" --no-check "$COMPILER_DIR/main.sf" "$COMPILER_DIR/$src.sf" "$BUILD_DIR/stage2/${src}.ll" \
            || fail "FULL" "Failed to compile $src.sf"
    done

    [[ "$VERBOSE" == true ]] && echo "  compile: runtime.sf"
    "$VM" --no-check "$COMPILER_DIR/main.sf" "$RUNTIME_SRC" "$BUILD_DIR/stage2/runtime.ll" \
        || fail "FULL" "Failed to compile runtime.sf"

    [[ "$VERBOSE" == true ]] && echo "  linking gen2..."
    clang -O2 -w -Wl,-stack_size,0x10000000 -o "$GEN2" \
        "$BUILD_DIR/stage2/main.ll" \
        "$BUILD_DIR/stage2/lexer.ll" \
        "$BUILD_DIR/stage2/parser.ll" \
        "$BUILD_DIR/stage2/codegen.ll" \
        "$BUILD_DIR/stage2/runtime.ll" \
        "$RUNTIME_BASE" \
        || fail "FULL" "Linking gen2 failed"

    pass "FULL" "gen2 rebuilt: $GEN2"
    echo ""
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

for src in "${SOURCES[@]}"; do
    [[ "$VERBOSE" == true ]] && echo "  compile: $src.sf"
    timeout 60 "$GEN2" --stdlib "$ROOT/src/lib" "$COMPILER_DIR/$src.sf" "$BUILD_DIR/stage3/${src}.ll" \
        || fail "STAGE 1" "gen2 failed to compile $src.sf"
done

[[ "$VERBOSE" == true ]] && echo "  compile: codegen.sf (assembled)"
timeout 60 "$GEN2" --stdlib "$ROOT/src/lib" "$BUILD_DIR/stage3/_codegen.sf" "$BUILD_DIR/stage3/codegen.ll" \
    || fail "STAGE 1" "gen2 failed to compile codegen.sf"

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
timeout 60 "$GEN2" --stdlib "$ROOT/src/lib" "$BUILD_DIR/stage3/_main.sf" "$BUILD_DIR/stage3/main.ll" \
    || fail "STAGE 1" "gen2 failed to compile main.sf"

# Compile runtime.sf
[[ "$VERBOSE" == true ]] && echo "  compile: runtime.sf"
timeout 60 "$GEN2" --stdlib "$ROOT/src/lib" "$RUNTIME_SRC" "$BUILD_DIR/stage3/runtime.ll" \
    || fail "STAGE 1" "gen2 failed to compile runtime.sf"

[[ "$VERBOSE" == true ]] && echo "  linking gen3..."
clang -O2 -w -Wl,-stack_size,0x10000000 -o "$BUILD_DIR/saffronc" \
    "$BUILD_DIR/stage3/main.ll" \
    "$BUILD_DIR/stage3/runtime.ll" \
    "$RUNTIME_BASE" \
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

clang -O2 -w -o "$BUILD_DIR/hello_bootstrap" "$BUILD_DIR/hello_bootstrap.ll" "$BUILD_DIR/stage3/runtime.ll" "$RUNTIME_BASE" \
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
