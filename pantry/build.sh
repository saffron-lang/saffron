#!/usr/bin/env bash
# Build pantry as a native binary using saffronc
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAFFRONC="$ROOT/build/saffronc"
RUNTIME="$ROOT/src/runtime.ll"
STUBS="$ROOT/src/runtime_stubs.ll"
BUILD_DIR="$ROOT/pantry/build"
SRC_DIR="$ROOT/pantry/src"

if [[ ! -x "$SAFFRONC" ]]; then
    echo "Error: saffronc not found at $SAFFRONC"
    echo "Run the bootstrap build first."
    exit 1
fi

mkdir -p "$BUILD_DIR/ll"

echo "Compiling pantry sources..."

# Compile each source file to LLVM IR
SOURCES=(
    "main"
    "config"
    "commands/init"
    "commands/run"
    "commands/build"
    "commands/test_cmd"
    "commands/add"
    "commands/remove"
    "commands/install"
    "commands/list"
)

LL_FILES=""
for src in "${SOURCES[@]}"; do
    name=$(basename "$src")
    echo "  $src.sf -> $name.ll"
    "$SAFFRONC" "$SRC_DIR/$src.sf" "$BUILD_DIR/ll/$name.ll"
    LL_FILES="$LL_FILES $BUILD_DIR/ll/$name.ll"
done

echo "Linking..."
clang -O2 -o "$BUILD_DIR/pantry" \
    $LL_FILES \
    "$RUNTIME" "$STUBS"

echo "Built: $BUILD_DIR/pantry"
echo ""
echo "Install with:"
echo "  cp $BUILD_DIR/pantry /usr/local/bin/"
