#!/usr/bin/env bash
# Regression check for BUGS #124 and #125 — wasm64 silently produces no output.
#
# WHY THIS IS A SEPARATE SCRIPT RATHER THAN A test/pass/*.sf FILE.
#
# tools/run_tests.sh has NO wasm coverage at all — not wasm32, not wasm64. It
# builds every test with the default (native) target, and `hello_wasm` is
# explicitly listed in its NOT_A_TEST set. tools/differential.sh does run a wasm
# configuration, but its ALL_CONFIGS is `native-O0 wasm32`, so wasm64 is not
# exercised anywhere in the tree. That is precisely why #124 and #125 — two
# undefined symbols that make ALL wasm64 output vanish — went unnoticed while
# the suite stayed green.
#
# Wiring wasm64 into run_tests.sh is the real fix for that gap and belongs to
# whoever owns that file; this script is the check itself, so it can be adopted
# by a hook later without being rewritten.
#
# THE CENTRAL POINT: LINKING PROVES NOTHING HERE.
#
# Both bugs are "a symbol is undefined and the linker turns it into a silent
# no-op host import" — the wasm64 link line passes `-Wl,--allow-undefined`
# (tools/saffron:357), which accepts EVERY missing symbol. So a successful build
# was the failing state. This script therefore RUNS each module and asserts on
# stdout; an empty stdout is a FAILURE, not a pass.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SAFFRON="$ROOT/tools/saffron"
RUNNER="$ROOT/tools/oracle/wasm_run.mjs"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fails=0

# check <label> <source-file> <expected-substring>
check() {
    local label="$1" src="$2" want="$3"
    local wasm="$TMP/$label.wasm"

    if ! "$SAFFRON" build "$src" --target wasm64 -o "$wasm" >"$TMP/$label.build" 2>&1; then
        echo "FAIL  $label — wasm64 build failed"
        sed 's/^/        /' "$TMP/$label.build" | grep -v 'Warning: unused variable' | tail -5
        fails=$((fails + 1))
        return
    fi

    local got
    got="$(node "$RUNNER" "$wasm" 2>"$TMP/$label.err")"

    if [[ -z "$got" ]]; then
        echo "FAIL  $label — module ran and produced NO output (the #124/#125 signature)"
        fails=$((fails + 1))
        return
    fi
    if [[ "$got" != *"$want"* ]]; then
        echo "FAIL  $label — expected to contain '$want', got: $(echo "$got" | tr '\n' '|')"
        fails=$((fails + 1))
        return
    fi
    echo "ok    $label — '$want'"
}

# #124: a file whose ENTIRE contents are `fun main()`. There are no top-level
# statements, so codegen emits no __saffron_entry; wasm_base.ll's _start must go
# through __saffron_boot, which calls __saffron_main for this shape. Keep this
# file free of top-level statements — a single top-level call flips
# has_top_level and exercises the other branch, which was never broken.
check main_only "$ROOT/scratch/w64/main_only.sf" "hello from main"

# #124's sibling branch: top-level code, so __saffron_entry DOES exist. Guards
# against fixing the main-only case by breaking this one.
check top_level "$ROOT/scratch/w64/top_level.sf" "hello top level"

# #125: output at all. A String argument is the case identity discipline can
# represent; see the wasm_base.ll comment for why a non-string cannot be, and
# the reported follow-up finding for what fixing that would take.
check interp "$ROOT/scratch/w64/interp.sf" "hello world"

if (( fails )); then
    echo "$fails wasm64 regression check(s) FAILED"
    exit 1
fi
echo "all wasm64 regression checks passed"
