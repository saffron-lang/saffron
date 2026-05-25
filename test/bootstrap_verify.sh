#!/usr/bin/env bash
# bootstrap_verify.sh — Verify that the Saffron compiler can bootstrap itself.
#
# A bootstrap verification proves the compiler is self-consistent:
#   Stage 1: The C VM interprets the compiler sources to produce LLVM IR.
#   Stage 2: The Stage 1 native binary compiles the same sources to LLVM IR.
#   If Stage 1 and Stage 2 outputs are identical, the compiler faithfully
#   reproduces itself — bootstrap is verified.
#
# Prerequisites:
#   - The C VM is built: cmake-build-debug/saffron
#   - clang is available on PATH (for linking .ll → native binary)
#   - The self-hosting compiler in src/compiler/ can:
#     (a) be run by the VM to emit LLVM IR (--no-check bypasses type checker)
#     (b) produce a native binary that accepts .sf files and emits .ll files
#
# NOTE: This script documents the intended bootstrap workflow. It may not
# fully succeed until the native code generation pipeline is complete.

set -euo pipefail

# --- Configuration -----------------------------------------------------------

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM="$ROOT/cmake-build-debug/saffron"
COMPILER_DIR="$ROOT/src/compiler"
BUILD_DIR="$ROOT/cmake-build-debug/bootstrap"

# Compiler source files (order matters for linking)
SOURCES=(lexer parser codegen)

# --- Helpers -----------------------------------------------------------------

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info()  { printf "${GREEN}[INFO]${NC}  %s\n" "$1"; }
warn()  { printf "${YELLOW}[WARN]${NC}  %s\n" "$1"; }
error() { printf "${RED}[FAIL]${NC}  %s\n" "$1"; }

die() {
    error "$1"
    exit 1
}

# --- Preflight checks --------------------------------------------------------

if [[ ! -x "$VM" ]]; then
    die "VM binary not found at $VM. Run: cmake -B cmake-build-debug && cmake --build cmake-build-debug"
fi

if ! command -v clang &>/dev/null; then
    die "clang not found on PATH. Install LLVM/Clang to link .ll files."
fi

for src in "${SOURCES[@]}"; do
    if [[ ! -f "$COMPILER_DIR/$src.sf" ]]; then
        die "Compiler source not found: $COMPILER_DIR/$src.sf"
    fi
done

# --- Setup -------------------------------------------------------------------

mkdir -p "$BUILD_DIR/stage1" "$BUILD_DIR/stage2"

info "Bootstrap verification starting"
info "Root: $ROOT"
info "Build dir: $BUILD_DIR"
echo ""

# =============================================================================
# Stage 1: Compile the compiler using the C VM
# =============================================================================

info "=== Stage 1: Compiling compiler sources via C VM ==="

for src in "${SOURCES[@]}"; do
    info "  Compiling $src.sf -> ${src}_s1.ll"
    "$VM" --no-check "$COMPILER_DIR/main.sf" -- "$COMPILER_DIR/$src.sf" \
        > "$BUILD_DIR/stage1/${src}_s1.ll" \
        || die "Stage 1 compilation failed for $src.sf"
done

info "  Linking stage 1 .ll files -> saffronc_s1"
clang -O2 -o "$BUILD_DIR/saffronc_s1" \
    "$BUILD_DIR/stage1/lexer_s1.ll" \
    "$BUILD_DIR/stage1/parser_s1.ll" \
    "$BUILD_DIR/stage1/codegen_s1.ll" \
    || die "Stage 1 linking failed"

info "Stage 1 complete: $BUILD_DIR/saffronc_s1"
echo ""

# =============================================================================
# Stage 2: Compile the compiler using the Stage 1 native binary
# =============================================================================

info "=== Stage 2: Compiling compiler sources via saffronc_s1 ==="

SAFFRONC_S1="$BUILD_DIR/saffronc_s1"

for src in "${SOURCES[@]}"; do
    info "  Compiling $src.sf -> ${src}_s2.ll"
    "$SAFFRONC_S1" "$COMPILER_DIR/$src.sf" \
        > "$BUILD_DIR/stage2/${src}_s2.ll" \
        || die "Stage 2 compilation failed for $src.sf"
done

info "  Linking stage 2 .ll files -> saffronc_s2"
clang -O2 -o "$BUILD_DIR/saffronc_s2" \
    "$BUILD_DIR/stage2/lexer_s2.ll" \
    "$BUILD_DIR/stage2/parser_s2.ll" \
    "$BUILD_DIR/stage2/codegen_s2.ll" \
    || die "Stage 2 linking failed"

info "Stage 2 complete: $BUILD_DIR/saffronc_s2"
echo ""

# =============================================================================
# Verification: Compare Stage 1 and Stage 2 outputs
# =============================================================================

info "=== Verifying bootstrap: comparing Stage 1 and Stage 2 IR ==="

ALL_MATCH=true

for src in "${SOURCES[@]}"; do
    S1="$BUILD_DIR/stage1/${src}_s1.ll"
    S2="$BUILD_DIR/stage2/${src}_s2.ll"

    if diff -q "$S1" "$S2" > /dev/null 2>&1; then
        info "  $src.ll: IDENTICAL"
    else
        error "  $src.ll: DIFFERS"
        diff --unified=3 "$S1" "$S2" | head -40
        ALL_MATCH=false
    fi
done

echo ""

if [[ "$ALL_MATCH" == true ]]; then
    printf "${GREEN}============================================${NC}\n"
    printf "${GREEN}  BOOTSTRAP VERIFIED!                       ${NC}\n"
    printf "${GREEN}  Stage 1 and Stage 2 produce identical IR. ${NC}\n"
    printf "${GREEN}============================================${NC}\n"
    exit 0
else
    printf "${RED}============================================${NC}\n"
    printf "${RED}  BOOTSTRAP FAILED                          ${NC}\n"
    printf "${RED}  Stage 1 and Stage 2 outputs differ.       ${NC}\n"
    printf "${RED}============================================${NC}\n"
    exit 1
fi
