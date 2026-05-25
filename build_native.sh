#!/bin/bash
# Build the self-hosted Saffron compiler as a native binary
# Uses the C VM to bootstrap the first native compilation

set -e

echo "=== Building Saffron Native Compiler ==="

# Step 1: Compile each source file separately
echo "[1/4] Compiling lexer..."
./cmake-build-debug/saffron --no-check src/compiler/main.sf src/compiler/lexer.sf /tmp/sfc_lexer.ll 2>/dev/null

echo "[2/4] Compiling parser..."
./cmake-build-debug/saffron --no-check src/compiler/main.sf src/compiler/parser.sf /tmp/sfc_parser.ll 2>/dev/null

echo "[3/4] Compiling codegen..."
./cmake-build-debug/saffron --no-check src/compiler/main.sf src/compiler/codegen.sf /tmp/sfc_codegen.ll 2>/dev/null

echo "[4/4] Compiling main..."
./cmake-build-debug/saffron --no-check src/compiler/main.sf src/compiler/main.sf /tmp/sfc_main.ll 2>/dev/null

# Step 2: Use the codegen .ll (has runtime) as the base and add a main
# For now, use the codegen standalone since it contains everything
echo "[link] Linking native binary..."
clang -O2 -o build/saffronc /tmp/sfc_codegen.ll 2>/dev/null

echo "=== Done! Binary at build/saffronc ==="
ls -la build/saffronc
