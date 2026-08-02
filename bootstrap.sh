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
# Overridable so you can bootstrap against a known-good gc.ll while someone
# else has the working copy mid-edit. tools/saffron already honors this.
RUNTIME_GC="${SAFFRON_RUNTIME_GC:-$ROOT/src/runtime/gc.ll}"
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

# Guard: the assembly must consume every *_body.sf that exists. A body file with
# no matching -e above would be silently dropped from the compiler — the exact
# failure mode the deleted mirror files used to cause.
for body in "$COMPILER_DIR"/codegen/*_body.sf; do
    part="$(basename "$body" _body.sf)"
    grep -q "@codegen-split: $part\b" "$COMPILER_DIR/codegen.sf" \
        || fail "ASSEMBLE" "codegen/${part}_body.sf has no '@codegen-split: $part' marker in codegen.sf"
    grep -q "codegen/${part}_body.sf" "$0" \
        || fail "ASSEMBLE" "codegen/${part}_body.sf is not read by the sed assembly above"
done

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
    cp "$COMPILER_DIR/resolve.sf" "$BUILD_DIR/stage3/resolve.sf"
    cp "$COMPILER_DIR/ast.sf" "$BUILD_DIR/stage3/ast.sf"
    # Rewrite the codegen import to use the assembled file
    sed -i '' 's|import "./codegen.sf" as Codegen|import "./_codegen.sf" as Codegen|' "$BUILD_DIR/stage3/_main.sf"
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
    cp "$COMPILER_DIR/resolve.sf" "$BUILD_DIR/stage3/resolve.sf"
    cp "$COMPILER_DIR/ast.sf" "$BUILD_DIR/stage3/ast.sf"
    sed -i '' 's|import "./codegen.sf" as Codegen|import "./_codegen.sf" as Codegen|' "$BUILD_DIR/stage3/_main.sf"
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
# Stage 2: gen3 → gen4 (the fixed point)
# =============================================================================
#
# Stage 1 proves gen2 accepts the source. It says nothing about gen3, which is
# the compiler everyone actually runs. Without this stage a gen3 that rejects the
# compiler's own source still gives a green bootstrap, because stage 1 never asks
# it: CLAUDE.md listed "gen3 can compile itself" among the gen2-promotion criteria
# and claimed the test stage verified it, but the test stage only ever compiled
# test/hello_bootstrap.sf — five lines of IO.println.
#
# That gap hid real defects. A checker fix that made ~100 latent non-exhaustive
# matches in the compiler visible (BUGS #76) produced 103 errors here while
# bootstrap.sh still reported success end to end. Adding this stage immediately
# found one that needed no checker change at all: codegen.sf:574 was a one-armed
# `match (stmts[si2]) { VarDecl(...) => n }` in a free function, which gen3 has
# been rejecting all along with nobody looking.
#
# The GEN2_OK=false fallback above already does a gen3→gen3' pass, so this is the
# same work on the path that normally runs, not new machinery.
#
# Skip with SKIP_GEN4=1 when you only need a gen3 to test with — it roughly
# doubles bootstrap time (measured 1m52 → 3m42). Don't skip it when deciding on a
# gen2 promotion; that is the one decision it exists for.

if [[ "${SKIP_GEN4:-0}" == "1" ]]; then
    info "STAGE 2" "SKIP_GEN4=1 — not verifying gen3 compiles itself"
    echo ""
else
    info "STAGE 2" "Verifying gen3 compiles its own source (gen4)..."
    GEN3="$BUILD_DIR/saffronc"
    mkdir -p "$BUILD_DIR/stage4"

    for src in "${SOURCES[@]}"; do
        [[ "$VERBOSE" == true ]] && echo "  compile (gen3): $src.sf"
        timeout 180 "$GEN3" --identity-mode --stdlib "$ROOT/src/lib" \
            "$COMPILER_DIR/$src.sf" "$BUILD_DIR/stage4/${src}.ll" \
            || fail "STAGE 2" "gen3 rejects $src.sf — it cannot compile itself. Diagnostics go to stdout, so run it directly: $GEN3 --identity-mode --stdlib $ROOT/src/lib $COMPILER_DIR/$src.sf /tmp/out.ll"
    done

    [[ "$VERBOSE" == true ]] && echo "  compile (gen3): codegen.sf (assembled)"
    timeout 180 "$GEN3" --identity-mode --stdlib "$ROOT/src/lib" \
        "$BUILD_DIR/stage3/_codegen.sf" "$BUILD_DIR/stage4/codegen.ll" \
        || fail "STAGE 2" "gen3 rejects the assembled codegen.sf — it cannot compile itself"

    # Same set of sibling modules main.sf imports as stage 1 above. If you add a
    # compiler module there, add it here too, or gen4 links against a stale copy.
    cp "$COMPILER_DIR/main.sf" "$BUILD_DIR/stage4/_main.sf"
    cp "$COMPILER_DIR/lexer.sf" "$BUILD_DIR/stage4/lexer.sf"
    cp "$COMPILER_DIR/parser.sf" "$BUILD_DIR/stage4/parser.sf"
    cp "$COMPILER_DIR/checker.sf" "$BUILD_DIR/stage4/checker.sf"
    cp "$COMPILER_DIR/resolve.sf" "$BUILD_DIR/stage4/resolve.sf"
    cp "$COMPILER_DIR/ast.sf" "$BUILD_DIR/stage4/ast.sf"
    cp "$BUILD_DIR/stage3/_codegen.sf" "$BUILD_DIR/stage4/_codegen.sf"
    sed -i '' 's|import "./codegen.sf" as Codegen|import "./_codegen.sf" as Codegen|' "$BUILD_DIR/stage4/_main.sf"

    [[ "$VERBOSE" == true ]] && echo "  compile (gen3): main.sf"
    timeout 180 "$GEN3" --identity-mode --stdlib "$ROOT/src/lib" \
        "$BUILD_DIR/stage4/_main.sf" "$BUILD_DIR/stage4/main.ll" \
        || fail "STAGE 2" "gen3 rejects main.sf — it cannot compile itself"

    [[ "$VERBOSE" == true ]] && echo "  compile (gen3): runtime.sf"
    timeout 180 "$GEN3" --identity-mode --stdlib "$ROOT/src/lib" \
        "$RUNTIME_SRC" "$BUILD_DIR/stage4/runtime.ll" \
        || fail "STAGE 2" "gen3 rejects runtime.sf"

    # Accepting the source is not the same as emitting IR that links.
    [[ "$VERBOSE" == true ]] && echo "  linking gen4..."
    clang -O2 -w -Wl,-stack_size,0x10000000 -o "$BUILD_DIR/stage4/saffronc" \
        "$BUILD_DIR/stage4/main.ll" \
        "$BUILD_DIR/stage4/runtime.ll" \
        "$RUNTIME_BASE" \
        "$RUNTIME_GC" \
        || fail "STAGE 2" "gen4 IR does not link — gen3 accepted the source but emitted bad IR"

    # And gen4 has to be a working compiler, not merely a binary that exists.
    cat > "$BUILD_DIR/stage4/probe.sf" << 'EOF'
var xs = [1, 2, 3]
IO.println("gen4 works: ${xs.length()}")
EOF
    timeout 120 "$BUILD_DIR/stage4/saffronc" "$BUILD_DIR/stage4/probe.sf" "$BUILD_DIR/stage4/probe.ll" \
        || fail "STAGE 2" "gen4 links but cannot compile a program"

    pass "STAGE 2" "gen3 compiles itself; gen4 links and compiles: $BUILD_DIR/stage4/saffronc"
    echo ""
fi

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

# A class plus an import — the smallest program that exercises the checker's
# register_decl path over a six-field ClassDecl. hello_bootstrap.sf above has
# neither, and that is exactly why BUGS #100 stayed invisible: a gen2 predating
# #96 emitted a register_decl that never stored ClassDecl's sixth field, so the
# gen3 it built segfaulted on any class, and both stages plus this test still
# came out green. gen4 could not catch it either — gen3 compiles gen4 with the
# fixed codegen, so gen4 is correct while gen3 is not.
info "TEST" "Checking gen3 type-checks a class reached through an import..."
GUARD_DIR="$BUILD_DIR/guard_lib"
mkdir -p "$GUARD_DIR"
echo 'fun guard_helper(): Int { return 2 }' > "$GUARD_DIR/guard_mod.sf"
cat > "$BUILD_DIR/guard.sf" << 'EOF'
import "@guard_mod" as GuardMod
class GuardClass {
    fun init() {}
}
IO.println(GuardMod.guard_helper())
EOF
"$BUILD_DIR/saffronc" --stdlib "$GUARD_DIR" "$BUILD_DIR/guard.sf" "$BUILD_DIR/guard.ll" \
    || fail "TEST" "gen3 cannot compile a class reached through an import — see BUGS #100. If gen3 works but the checked-in gen2 does not, gen2 needs the promotion ceremony in CLAUDE.md."

pass "TEST" "gen3 handles a class plus an import."
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
