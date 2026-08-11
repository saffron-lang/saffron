#!/usr/bin/env bash
# run_tests.sh — run the Sumac test suite through the real compile+link+run
# pipeline (tools/saffron run), from the repo root.
#
# Runs every sumac/test/test_*.sf file. The smoke_*.sf files are interactive /
# manual (they drive a real TTY loop) and are deliberately skipped.
#
# Each test is a @test suite: it prints an "All N assertions passed" line on
# success, or "FAIL:" / "N/M passed, K failed" on assertion failure. A test also
# fails on a nonzero exit, a "Runtime Error", or a timeout. One green/red line is
# printed per test, then a summary; the script exits nonzero if any test failed.
#
# Usage:
#   bash sumac/tools/run_tests.sh            run all sumac/test/test_*.sf
#   bash sumac/tools/run_tests.sh -v         print the captured log for failures
#
# Env: RUN_TIMEOUT (default 120s) bounds each compile+run.

set -o pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SAFFRON="$ROOT/tools/saffron"
TESTDIR="$ROOT/sumac/test"

RUN_TIMEOUT=${RUN_TIMEOUT:-120}
VERBOSE=false

for arg in "$@"; do
    case "$arg" in
        -v|--verbose) VERBOSE=true ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) echo "run_tests.sh: unknown argument '$arg'" >&2; exit 2 ;;
    esac
done

if [[ ! -x "$SAFFRON" ]]; then
    echo "run_tests.sh: tools/saffron not found at $SAFFRON" >&2
    exit 2
fi

TMPDIR=$(mktemp -d /tmp/sumac_tests_XXXXXX)
trap 'rm -rf "$TMPDIR"' EXIT

# ANSI colors (only when stdout is a tty).
if [[ -t 1 ]]; then
    GREEN=$'\033[32m'; RED=$'\033[31m'; DIM=$'\033[2m'; RESET=$'\033[0m'
else
    GREEN=""; RED=""; DIM=""; RESET=""
fi

PASS=0
FAIL=0
FAILURES=()

echo "=== Sumac test suite (sumac/test/test_*.sf) ==="

shopt -s nullglob
for f in "$TESTDIR"/test_*.sf; do
    name=$(basename "$f" .sf)
    log="$TMPDIR/$name.log"

    # tools/saffron run compiles, links and runs the file in one shot.
    ( cd "$ROOT" && timeout "$RUN_TIMEOUT" "$SAFFRON" run "$f" ) >"$log" 2>&1
    ec=$?

    # Strip codegen/checker warnings before scanning for failure markers: they
    # carry the literal word "failed" ("type inference failed") and would
    # otherwise read as false negatives.
    out=$(grep -vE '^\[(codegen|checker)\] Warning' "$log" 2>/dev/null)

    reason=""
    if [[ $ec -eq 124 ]]; then
        reason="timeout after ${RUN_TIMEOUT}s"
    elif grep -q 'Runtime Error:' <<<"$out"; then
        reason=$(grep -m1 'Runtime Error:' <<<"$out" | cut -c1-100)
    elif grep -qE '^\s*FAIL:|[0-9]+/[0-9]+ passed, [1-9][0-9]* failed' <<<"$out"; then
        n=$(grep -cE '^\s*FAIL:' <<<"$out")
        reason="${n} failed assertion(s)"
    elif [[ $ec -ne 0 ]]; then
        # No explicit marker but a nonzero exit — surface the first error-ish line.
        detail=$(grep -m1 -iE 'error' <<<"$out" | cut -c1-100)
        reason="exit $ec${detail:+ — $detail}"
    fi

    if [[ -z "$reason" ]]; then
        printf '%sPASS%s  %s\n' "$GREEN" "$RESET" "$name"
        PASS=$((PASS + 1))
    else
        printf '%sFAIL%s  %-24s %s%s%s\n' "$RED" "$RESET" "$name" "$DIM" "$reason" "$RESET"
        FAIL=$((FAIL + 1))
        FAILURES+=("$name|$reason|$log")
    fi
done

if [[ $((PASS + FAIL)) -eq 0 ]]; then
    echo "run_tests.sh: no test_*.sf files found in $TESTDIR" >&2
    exit 2
fi

if [[ ${#FAILURES[@]} -gt 0 ]]; then
    echo ""
    echo "=== Failures ==="
    for entry in "${FAILURES[@]}"; do
        IFS='|' read -r name reason log <<<"$entry"
        printf '  %-24s %s\n' "$name" "$reason"
        if [[ "$VERBOSE" == true && -s "$log" ]]; then
            sed 's/^/        | /' "$log"
        fi
    done
fi

echo ""
echo "TOTAL: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
