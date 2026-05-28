#!/usr/bin/env bash
# Run all test/*.sf files through the native compiler
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAFFRONC="$ROOT/build/saffronc"
RUNTIME="$ROOT/build/stage3/runtime.ll"
BASE="$ROOT/src/runtime/base.ll"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

PASS=0
FAIL=0
SKIP=0

for f in "$ROOT"/test/*.sf; do
    name=$(basename "$f" .sf)
    printf "%-40s " "$name"

    # Skip known problematic tests (async, imports that need special handling)
    case "$name" in
        async*|imports|hello_wasm|goals|decorators)
            echo "SKIP"
            SKIP=$((SKIP + 1))
            continue
            ;;
    esac

    # Compile
    if ! timeout 10 "$SAFFRONC" "$f" "$TMPDIR/${name}.ll" > /dev/null 2>&1; then
        echo "FAIL (compile)"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Link
    if ! clang -O0 -w -Wl,-stack_size,0x4000000 -o "$TMPDIR/${name}" "$TMPDIR/${name}.ll" "$RUNTIME" "$BASE" > /dev/null 2>&1; then
        echo "FAIL (link)"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Run
    if timeout 5 "$TMPDIR/${name}" > /dev/null 2>&1; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (run)"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
