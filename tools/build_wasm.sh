#!/usr/bin/env bash
# build_wasm.sh — Compile a Saffron program to WASM and generate HTML loader
#
# Usage: ./tools/build_wasm.sh input.sf [output_dir]
#
# Produces:
#   output_dir/app.wasm    — The compiled WASM binary
#   output_dir/index.html  — HTML page that loads and runs the WASM
#   output_dir/loader.js   — JS glue module

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAFFRONC="$ROOT/build/saffronc"
RUNTIME_SRC="$ROOT/src/runtime/runtime.sf"
WASM_BASE="$ROOT/src/runtime/wasm_base.ll"
CLANG="/opt/homebrew/opt/llvm/bin/clang"
WASM_LD="/opt/homebrew/bin/wasm-ld"

# --- Args ---

INPUT="${1:-}"
OUTPUT_DIR="${2:-build/wasm}"

if [[ -z "$INPUT" ]]; then
    echo "Usage: ./tools/build_wasm.sh <input.sf> [output_dir]"
    exit 1
fi

if [[ ! -f "$INPUT" ]]; then
    echo "Error: File not found: $INPUT"
    exit 1
fi

if [[ ! -x "$SAFFRONC" ]]; then
    echo "Error: saffronc not found at $SAFFRONC. Run ./bootstrap.sh first."
    exit 1
fi

if [[ ! -x "$CLANG" ]]; then
    echo "Error: LLVM clang not found at $CLANG. Install with: brew install llvm"
    exit 1
fi

if [[ ! -x "$WASM_LD" ]]; then
    echo "Error: wasm-ld not found at $WASM_LD. Install with: brew install lld"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# --- Compile ---

echo "[1/4] Compiling $INPUT → LLVM IR (wasm32)..."
"$SAFFRONC" --target wasm32 "$INPUT" "$OUTPUT_DIR/app.ll"

echo "[2/4] Compiling runtime → LLVM IR (wasm32)..."
"$SAFFRONC" --target wasm32 "$RUNTIME_SRC" "$OUTPUT_DIR/runtime.ll"

echo "[3/4] Linking → app.wasm..."
"$CLANG" --target=wasm32-unknown-unknown -nostdlib -O2 \
    -fuse-ld="$WASM_LD" \
    -Wl,--export=_start \
    -Wl,--export=malloc \
    -Wl,--no-entry \
    -Wl,--allow-undefined \
    -Wl,--allow-multiple-definition \
    -o "$OUTPUT_DIR/app.wasm" \
    "$WASM_BASE" \
    "$OUTPUT_DIR/runtime.ll" \
    "$OUTPUT_DIR/app.ll"

echo "[4/4] Generating HTML + JS loader..."

# --- Generate loader.js ---
cat > "$OUTPUT_DIR/loader.js" << 'LOADEREOF'
export async function loadSaffron(wasmUrl) {
    function readCString(mem, ptr) {
        const bytes = new Uint8Array(mem.buffer);
        let end = ptr;
        while (end < bytes.length && bytes[end] !== 0) end++;
        return new TextDecoder().decode(bytes.slice(ptr, end));
    }

    let instance;
    const imports = {
        env: {
            js_log_str: (ptr) => {
                const s = readCString(instance.exports.memory, ptr);
                console.log(s);
                const el = document.getElementById('output');
                if (el) el.textContent += s + '\n';
            },
            js_log_int: (n) => {
                console.log(Number(n));
                const el = document.getElementById('output');
                if (el) el.textContent += n + '\n';
            },
            js_log_bool: (b) => {
                const s = b ? "true" : "false";
                console.log(s);
                const el = document.getElementById('output');
                if (el) el.textContent += s + '\n';
            },
            js_log_nil: () => {
                console.log("nil");
                const el = document.getElementById('output');
                if (el) el.textContent += 'nil\n';
            },
            __builtin_trap: () => { throw new Error("Saffron: exit called"); },
        },
    };

    const response = await fetch(wasmUrl);
    const wasmBytes = await response.arrayBuffer();
    const result = await WebAssembly.instantiate(wasmBytes, imports);
    instance = result.instance;
    return instance;
}
LOADEREOF

# --- Generate index.html ---
BASENAME=$(basename "$INPUT" .sf)
cat > "$OUTPUT_DIR/index.html" << HTMLEOF
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>$BASENAME — Saffron WASM</title>
    <style>
        body { font-family: monospace; background: #1a1a2e; color: #e0e0e0; padding: 2rem; }
        h1 { color: #4fc3f7; }
        #output { background: #0f0f1a; padding: 1rem; border-radius: 8px; white-space: pre; min-height: 100px; }
    </style>
</head>
<body>
    <h1>$BASENAME.sf → WASM</h1>
    <div id="output"></div>
    <script type="module">
        import { loadSaffron } from './loader.js';
        try {
            const instance = await loadSaffron('./app.wasm');
            instance.exports._start();
        } catch (e) {
            document.getElementById('output').textContent += '\\nError: ' + e.message;
        }
    </script>
</body>
</html>
HTMLEOF

# --- Summary ---
WASM_SIZE=$(wc -c < "$OUTPUT_DIR/app.wasm" | tr -d ' ')
echo ""
echo "✓ Build complete!"
echo "  WASM: $OUTPUT_DIR/app.wasm ($WASM_SIZE bytes)"
echo "  HTML: $OUTPUT_DIR/index.html"
echo ""
echo "  To run: cd $OUTPUT_DIR && python3 -m http.server 8080"
echo "  Then open: http://localhost:8080"
