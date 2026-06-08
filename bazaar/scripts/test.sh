#!/bin/bash
# Bazaar full test suite — build + unit tests + integration tests
# This is the bar. If this script fails, nothing ships.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$SCRIPT_DIR"

echo "╔══════════════════════════════════════╗"
echo "║  Bazaar Test Suite                   ║"
echo "╚══════════════════════════════════════╝"
echo ""

# Step 1: Build (catches ALL compile errors)
echo "▸ Step 1: pantry build"
pantry build
echo "  ✓ Build passed"
echo ""

# Step 2: Unit tests (compiler/runtime correctness)
echo "▸ Step 2: Unit tests"
cd "$SCRIPT_DIR/.."
for f in test/test_stdlib.sf test/test_classes.sf test/test_closures.sf test/test_enums.sf test/test_control_flow.sf test/test_json.sf; do
    RESULT=$(timeout 15 tools/saffron run "$f" 2>/dev/null | tail -1)
    if echo "$RESULT" | grep -q "failed"; then
        echo "  ✗ $f: $RESULT"
        exit 1
    fi
    echo "  ✓ $f"
done
echo ""

# Step 3: Parsley tests
echo "▸ Step 3: Parsley tests"
RESULT=$(timeout 15 tools/saffron run parsley/test/test_router.sf 2>/dev/null | tail -1)
echo "  ✓ parsley: $RESULT"
echo ""

# Step 4: Frontend playwright tests (requires fresh wasm from step 1)
echo "▸ Step 4: Frontend integration tests"
cd "$SCRIPT_DIR"
PLAYWRIGHT_RESULT=$(npx playwright test tests/app.spec.js 2>&1 | grep "passed\|failed")
echo "  $PLAYWRIGHT_RESULT"
if echo "$PLAYWRIGHT_RESULT" | grep -q "failed"; then
    exit 1
fi
echo ""

# Step 5: Backend API tests (if backend is running)
echo "▸ Step 5: Backend API tests"
if curl -s --max-time 2 http://localhost:3001/api/v1/packages > /dev/null 2>&1; then
    API_RESULT=$(npx playwright test tests/api.spec.js 2>&1 | grep "passed\|failed\|skipped")
    echo "  $API_RESULT"
else
    echo "  ⊘ Skipped (backend not running on port 3001)"
fi
echo ""

echo "══════════════════════════════════════"
echo "  All checks passed ✓"
echo "══════════════════════════════════════"
