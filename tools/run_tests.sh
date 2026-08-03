#!/usr/bin/env bash
# run_tests.sh — run the Saffron test suites through the real compile+link pipeline.
#
# Suites:
#   test/*.sf        smoke / feature tests. Must compile, link and run cleanly.
#   test/pass/*.sf   conformance suite. Must compile AND run successfully.
#   test/fail/*.sf   negative conformance suite. The compiler MUST reject these.
#                    A fail-suite file that compiles cleanly is itself a failure.
#
# Usage:
#   tools/run_tests.sh                 all suites (network + stale tests skipped)
#   tools/run_tests.sh main            only test/*.sf
#   tools/run_tests.sh pass            only test/pass/*.sf
#   tools/run_tests.sh fail            only test/fail/*.sf
#   tools/run_tests.sh --network       also run the network-dependent tests
#   tools/run_tests.sh --stale         also run the known-stale tests
#   tools/run_tests.sh -v              print the captured log for every failure
#
# A test named in KNOWN_FAIL still RUNS; its failure is reported as `xfail`
# against a BUGS number and does not fail the suite, but its unexpected SUCCESS
# (`xpass`) does — see that list for why.
#
# NOTE: compilation and linking are delegated to tools/saffron (`saffron build`)
# rather than reimplemented here. The link line needs build/stage3/runtime.ll,
# src/runtime/gc.ll, src/runtime/base_nanbox.ll, four native .c files and
# -lssl -lcrypto; duplicating it here is what previously made this script report
# a bogus "0 passed, 105 failed" (it linked base.ll instead of base_nanbox.ll).
# Delegating means the runner can never drift from the real pipeline again.

# Deliberately no `set -e`: individual test failures must not abort the run.
# No `set -u` either — this must work under macOS's stock bash 3.2, which has
# no associative arrays and errors on empty-array expansion under `set -u`.
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAFFRON="$ROOT/tools/saffron"
SAFFRONC="${SAFFRONC:-$ROOT/build/saffronc}"

BUILD_TIMEOUT=${BUILD_TIMEOUT:-60}
RUN_TIMEOUT=${RUN_TIMEOUT:-10}

TMPDIR=$(mktemp -d /tmp/saffron_tests_XXXXXX)
trap 'rm -rf "$TMPDIR"' EXIT

# --- Test classification -----------------------------------------------------

# Tests that need live network access (httpbin.org, example.com, DNS, ...).
# Skipped by default: they fail offline for reasons unrelated to the compiler.
NETWORK_TESTS="test_httpx test_async_io test_dns test_net"

# Tests written against syntax the compiler no longer accepts (e.g. `var x = 5`
# with no annotation, C-style `for (var i = 0; ...)`). These are STALE, not
# broken: a human needs to decide whether to modernise or delete them.
# Do NOT "fix" the compiler to accept these.
STALE_TESTS="loops builtin_types for_in types runner decorators any_bug_repro"

# Aspirational / non-native-target files that are not runnable tests.
#
# gc_generational_test exercises the nursery, which __gc_init deliberately no
# longer starts (BUGS #63/#81 — the moving minor collector cannot be correct
# against codegen that holds receivers in SSA temps). Its four assertions test
# for bump allocation and minor collections, so they fail by design while the
# nursery is off; the file is kept because it is exactly the test to re-enable
# alongside a non-moving young generation.
NOT_A_TEST="goals hello_wasm gc_generational_test"

# Tests that fail ON PURPOSE because the bug they pin is still open.
#
# A test listed here is NOT skipped: it is compiled and run exactly like every
# other test, and its failure is reported as `xfail` without turning the suite
# red. What that buys over adding it to STALE_TESTS — which skips — is the other
# direction. If a known-fail test starts PASSING it is reported as `xpass` and
# DOES turn the suite red, so the entry must be removed in the same change that
# fixes the bug. A skip can never notice a fix, and a permanently-red test
# trains people to ignore the suite; this notices both ways.
#
# This is for a test that is red because the BUG is open, not because the test
# is wrong. Do not park a broken test here — write it so it asserts the
# invariant the fix must satisfy, then list it.
#
# Format: one "<label> <BUGS number>" per line. The label is exactly what the
# suite prints: a bare name for test/*.sf, "pass/<name>" for test/pass/*.sf.
# An entry must land in the same commit as the test it names, or the staleness
# check below will (correctly) flag it.
KNOWN_FAIL="
oracle_println_class_not_bits 115
"

known_fail_bug() {   # label -> echoes the BUGS number, or nothing
    local label="$1" l bug
    while read -r l bug; do
        [[ -z "$l" ]] && continue
        if [[ "$l" == "$label" ]]; then echo "$bug"; echo "$label" >>"$KF_SEEN"; return 0; fi
    done <<<"$KNOWN_FAIL"
    return 1
}

# Tests that intentionally return their computed result as the process exit
# code (mini_while exits 55 = fib(10)). A nonzero exit is CORRECT for these,
# so exit status alone cannot decide pass/fail — only crashes and runtime
# errors count as failures.
exit_code_is_result() {
    case "$1" in
        mini_*) return 0 ;;
        *) return 1 ;;
    esac
}

in_list() {   # needle space-separated-list
    local needle="$1" item
    for item in $2; do
        [[ "$item" == "$needle" ]] && return 0
    done
    return 1
}

# --- Options -----------------------------------------------------------------

RUN_MAIN=true
RUN_PASS=true
RUN_FAIL=true
WITH_NETWORK=false
WITH_STALE=false
VERBOSE=false

if [[ $# -gt 0 ]]; then
    explicit_suite=false
    for arg in "$@"; do
        case "$arg" in
            main|pass|fail) explicit_suite=true ;;
        esac
    done
    if [[ "$explicit_suite" == true ]]; then
        RUN_MAIN=false; RUN_PASS=false; RUN_FAIL=false
    fi
    for arg in "$@"; do
        case "$arg" in
            main) RUN_MAIN=true ;;
            pass) RUN_PASS=true ;;
            fail) RUN_FAIL=true ;;
            --network) WITH_NETWORK=true ;;
            --stale) WITH_STALE=true ;;
            -v|--verbose) VERBOSE=true ;;
            -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
            *) echo "run_tests.sh: unknown argument '$arg'" >&2; exit 2 ;;
        esac
    done
fi

if [[ ! -x "$SAFFRONC" ]]; then
    echo "run_tests.sh: compiler not found at $SAFFRONC (build it with ./bootstrap.sh)" >&2
    exit 2
fi

# --- Counters ----------------------------------------------------------------

FAILURES=()
XFAILS=()
STALE_SEEN=()

SUITE_PASS=0
SUITE_FAIL=0
SUITE_SKIP=0
SUITE_XFAIL=0
TOTAL_PASS=0
TOTAL_FAIL=0
TOTAL_SKIP=0
TOTAL_XFAIL=0

# bash 3.2 has no associative arrays: keep category tallies in a newline-
# delimited "category" log and count with grep at the end.
CATEGORY_LOG="$TMPDIR/categories"
: >"$CATEGORY_LOG"
bump() { echo "$1" >>"$CATEGORY_LOG"; }

# Which KNOWN_FAIL labels were actually reached. An entry naming a test that no
# longer exists (renamed, deleted, or moved between suites) would otherwise rot
# invisibly — and a stale entry is exactly what suppresses a real failure later.
KF_SEEN="$TMPDIR/known_fail_seen"
: >"$KF_SEEN"
# grep -c always prints a count (and exits 1 when it is zero), so read it via
# command substitution rather than relying on the exit status.
count_category() { grep -cx "$1" "$CATEGORY_LOG" 2>/dev/null; true; }

record_pass() {   # category label
    # A KNOWN_FAIL test that passes is an XPASS: the bug is fixed and the entry
    # is now lying. That is a suite failure, deliberately, so the list cannot
    # rot into a set of tests nobody rechecks.
    local plabel="${2%% (*}" bug
    if bug=$(known_fail_bug "$plabel"); then
        printf 'XPASS %-18s %s  — BUGS #%s looks FIXED: remove it from KNOWN_FAIL\n' \
               "unexpected-pass" "$plabel" "$bug"
        bump "xpass"
        SUITE_FAIL=$((SUITE_FAIL + 1))
        FAILURES+=("xpass|$plabel|BUGS #$bug looks fixed — drop the KNOWN_FAIL entry")
        return
    fi
    printf 'PASS         %s\n' "$2"
    bump "$1"
    SUITE_PASS=$((SUITE_PASS + 1))
}

record_fail() {   # category label detail logfile
    local bug
    if bug=$(known_fail_bug "$2"); then
        printf 'XFAIL %-18s %s  — BUGS #%s%s\n' "$1" "$2" "$bug" "${3:+, $3}"
        bump "xfail"
        SUITE_XFAIL=$((SUITE_XFAIL + 1))
        XFAILS+=("$2|$bug|$1${3:+: $3}")
        return
    fi
    printf 'FAIL  %-18s %s%s\n' "$1" "$2" "${3:+  — $3}"
    bump "$1"
    SUITE_FAIL=$((SUITE_FAIL + 1))
    FAILURES+=("$1|$2|$3")
    if [[ "$VERBOSE" == true && -n "${4:-}" && -s "${4:-}" ]]; then
        sed 's/^/        | /' "$4"
    fi
}

record_skip() {   # category name reason
    printf 'SKIP  %-18s %s\n' "$1" "$2"
    bump "$1"
    SUITE_SKIP=$((SUITE_SKIP + 1))
}

# Strip codegen warnings before scanning output for failure markers. These
# contain the literal word "failed" ("[codegen] Warning: ... (type inference
# failed)") and used to produce false negatives.
filter_noise() {
    grep -vE '^\[(codegen|checker)\] Warning' "$1" 2>/dev/null || true
}

# Classify a failed `saffron build`: compile error vs link error vs invalid IR.
classify_build_failure() {   # logfile
    local log="$1"
    # Order matters: the compiler bails before linking, and a real link failure
    # never contains .ll parse diagnostics.
    if grep -q 'saffron: compilation failed' "$log"; then
        echo "compile-error"
    elif grep -qE 'Undefined symbols|^ld: |duplicate symbol|library not found' "$log"; then
        echo "link-error"
    elif grep -qE '\.ll:[0-9]+:[0-9]+: error:|expected top-level entity|error: use of undefined value' "$log"; then
        echo "invalid-ir"
    else
        echo "compile-error"
    fi
}

# --- Positive test: must compile, link and run --------------------------------

run_positive_test() {   # file label
    local f="$1" label="$2"
    local name; name=$(basename "$f" .sf)
    local bin="$TMPDIR/bin_${label//\//_}_$name"
    local buildlog="$TMPDIR/${name}.build.log"
    local runlog="$TMPDIR/${name}.run.log"

    timeout "$BUILD_TIMEOUT" "$SAFFRON" build "$f" -o "$bin" >"$buildlog" 2>&1
    local build_ec=$?

    if [[ $build_ec -eq 124 ]]; then
        record_fail "timeout" "$label" "compiler exceeded ${BUILD_TIMEOUT}s" "$buildlog"
        return
    fi
    if [[ $build_ec -ne 0 || ! -x "$bin" ]]; then
        local cat; cat=$(classify_build_failure "$buildlog")
        local detail
        if [[ "$cat" == "link-error" ]]; then
            # Report the undefined symbol, not clang's generic "linker command
            # failed" line, which says nothing useful.
            detail=$(grep -m3 -oE '"_[A-Za-z0-9_]+", referenced' "$buildlog" \
                     | sed 's/", referenced//;s/"//' | tr '\n' ' ')
            detail="undefined: ${detail:-unknown}"
        else
            detail=$(filter_noise "$buildlog" | grep -m1 -E 'error|Error|ERROR' | cut -c1-100)
        fi
        record_fail "$cat" "$label" "$detail" "$buildlog"
        return
    fi

    timeout "$RUN_TIMEOUT" "$bin" >"$runlog" 2>&1 </dev/null
    local run_ec=$?
    local out; out=$(filter_noise "$runlog")

    if [[ $run_ec -eq 124 ]]; then
        record_fail "timeout" "$label" "hung for >${RUN_TIMEOUT}s" "$runlog"
        return
    fi
    if [[ $run_ec -eq 139 ]]; then
        record_fail "segfault" "$label" "exit 139" "$runlog"
        return
    fi
    if [[ $run_ec -gt 128 ]]; then
        record_fail "crash-signal" "$label" "exit $run_ec (signal $((run_ec - 128)))" "$runlog"
        return
    fi
    if grep -q 'Runtime Error:' <<<"$out"; then
        record_fail "runtime-error" "$label" "$(grep -m1 'Runtime Error:' <<<"$out" | cut -c1-100)" "$runlog"
        return
    fi
    if grep -qE '^\s*FAIL:|[0-9]+/[0-9]+ passed, [1-9][0-9]* failed' <<<"$out"; then
        local n; n=$(grep -cE '^\s*FAIL:' <<<"$out")
        record_fail "assertion-failure" "$label" "${n} failed assertion(s)" "$runlog"
        return
    fi
    if [[ $run_ec -ne 0 ]] && ! exit_code_is_result "$name"; then
        record_fail "nonzero-exit" "$label" "exit $run_ec" "$runlog"
        return
    fi

    # Exit-status check. The gate above deliberately ignores the exit code of an
    # `exit_code_is_result` test, because for those a nonzero status is the answer
    # and not a failure. The cost was that four of them — mini_1param,
    # mini_arithmetic, mini_ifelse, mini_while — had NOTHING checked whatsoever:
    # they print no output, so the .expected diff below has nothing to compare,
    # and the one value each computes was thrown away. `fun main(): Int { return
    # 0 }` passed all four (BUGS #107). A sibling `<name>.exit` file holding the
    # expected status makes that value load-bearing, and applies to any test, not
    # just the mini_* set.
    local expected_exit="${f%.sf}.exit"
    if [[ -f "$expected_exit" ]]; then
        local want; want=$(tr -d '[:space:]' <"$expected_exit")
        if [[ "$run_ec" != "$want" ]]; then
            record_fail "exit-mismatch" "$label" "exit $run_ec, expected $want" "$runlog"
            return
        fi
    fi

    # Expected-output check. Every test above this line is an exit-code check, so
    # a test whose value *is* its output could truncate silently and still pass:
    # `test_async.sf` was green for the entire life of BUGS #38 while emitting 2
    # of its ~12 expected lines and garbage for the rest. A sibling `<name>.expected`
    # file, when present, is diffed against stdout+stderr with codegen warnings
    # stripped. Assertion-based tests need no such file — @test already sets a
    # non-zero exit — so this stays opt-in per test rather than becoming a
    # blanket requirement that would have to be back-filled for 165 files.
    #
    # THE TWO MECHANISMS COMPOSE. Assertions and `.expected` are not alternatives,
    # and carrying both is supported: the assertion gate above returns early only
    # when an assertion actually FAILED, so a test whose assertions all pass still
    # falls through to the diff below. BUGS #107's survey found 0 of 174 tests
    # carrying both, which reads like a policy and is not one — it is an accident
    # of how the two were adopted. Prefer both where a test has real invariants AND
    # a stable full output: the assertion says *why* a value is right, the
    # `.expected` catches everything the assertions forgot to mention.
    #
    # The one case to NOT record is an output that is currently WRONG. Freezing a
    # known-bad output is worse than leaving a test blind, because the eventual fix
    # then reads as a regression. test/pass/enums.sf and test/pass/generics.sf are
    # deliberately assertion-only for this reason (BUGS #105).
    local expected="${f%.sf}.expected"
    if [[ -f "$expected" ]]; then
        # filter_noise strips trailing structure, so compare through the same
        # normalization on both sides: `printf '%s\n' ""` emits a blank line,
        # which would make an empty-output test differ from an empty .expected.
        if ! diff -q <(filter_noise "$runlog") "$expected" >/dev/null 2>&1; then
            local firstdiff
            firstdiff=$(diff <(filter_noise "$runlog") "$expected" | head -3 | tr '\n' ' ' | cut -c1-100)
            record_fail "output-mismatch" "$label" "$firstdiff" "$runlog"
            return
        fi
    fi

    record_pass "pass" "$label"
}

# --- Negative test: the compiler must REJECT it -------------------------------

run_negative_test() {   # file label
    local f="$1" label="$2"
    local name; name=$(basename "$f" .sf)
    local log="$TMPDIR/neg_${name}.log"

    timeout "$BUILD_TIMEOUT" "$SAFFRONC" --stdlib "$ROOT/src/lib" "$f" "$TMPDIR/neg_${name}.ll" >"$log" 2>&1
    local ec=$?

    if [[ $ec -eq 124 ]]; then
        record_fail "timeout" "$label" "compiler exceeded ${BUILD_TIMEOUT}s" "$log"
        return
    fi

    local out; out=$(filter_noise "$log")
    if [[ $ec -ne 0 ]]; then
        record_pass "pass" "$label (rejected, exit $ec)"
        return
    fi
    if grep -qE '^ERROR|Error:|error:' <<<"$out"; then
        record_pass "pass" "$label (diagnostics emitted, exit 0)"
        return
    fi

    record_fail "not-rejected" "$label" "compiled cleanly but must be an error"  "$log"
}

# --- Suite drivers -----------------------------------------------------------

suite_header() {
    SUITE_PASS=0; SUITE_FAIL=0; SUITE_SKIP=0; SUITE_XFAIL=0
    echo ""
    echo "=== $1 ==="
}

suite_footer() {
    local xf=""
    [[ $SUITE_XFAIL -gt 0 ]] && xf=", $SUITE_XFAIL known-fail"
    echo "--- $1: $SUITE_PASS passed, $SUITE_FAIL failed, $SUITE_SKIP skipped$xf"
    TOTAL_PASS=$((TOTAL_PASS + SUITE_PASS))
    TOTAL_FAIL=$((TOTAL_FAIL + SUITE_FAIL))
    TOTAL_SKIP=$((TOTAL_SKIP + SUITE_SKIP))
    TOTAL_XFAIL=$((TOTAL_XFAIL + SUITE_XFAIL))
}

if [[ "$RUN_MAIN" == true ]]; then
    suite_header "test/*.sf"
    for f in "$ROOT"/test/*.sf; do
        [[ -f "$f" ]] || continue
        name=$(basename "$f" .sf)

        if in_list "$name" "$NOT_A_TEST"; then
            record_skip "not-a-test" "$name" "aspirational / non-native target"
            continue
        fi
        if in_list "$name" "$STALE_TESTS"; then
            STALE_SEEN+=("$name")
            if [[ "$WITH_STALE" != true ]]; then
                record_skip "stale" "$name" "uses syntax the compiler no longer accepts"
                continue
            fi
        fi
        if in_list "$name" "$NETWORK_TESTS" && [[ "$WITH_NETWORK" != true ]]; then
            record_skip "network" "$name" "requires live network (pass --network to run)"
            continue
        fi

        run_positive_test "$f" "$name"
    done
    suite_footer "test/*.sf"
fi

if [[ "$RUN_PASS" == true ]]; then
    suite_header "test/pass/*.sf (must compile AND run)"
    for f in "$ROOT"/test/pass/*.sf; do
        [[ -f "$f" ]] || continue
        run_positive_test "$f" "pass/$(basename "$f" .sf)"
    done
    suite_footer "test/pass/*.sf"
fi

if [[ "$RUN_FAIL" == true ]]; then
    suite_header "test/fail/*.sf (must be REJECTED by the compiler)"
    for f in "$ROOT"/test/fail/*.sf; do
        [[ -f "$f" ]] || continue
        run_negative_test "$f" "fail/$(basename "$f" .sf)"
    done
    suite_footer "test/fail/*.sf"
fi

# --- Summary -----------------------------------------------------------------

echo ""
echo "=== Category breakdown ==="
for cat in pass compile-error link-error invalid-ir segfault crash-signal \
           assertion-failure runtime-error nonzero-exit exit-mismatch \
           output-mismatch timeout not-rejected network stale not-a-test \
           xfail xpass; do
    n=$(count_category "$cat")
    [[ ${n:-0} -gt 0 ]] && printf '  %-18s %d\n' "$cat" "$n"
done

if [[ ${#STALE_SEEN[@]} -gt 0 ]]; then
    echo ""
    echo "=== Stale tests (human decision needed: modernise or delete) ==="
    for s in "${STALE_SEEN[@]}"; do echo "  test/$s.sf"; done
fi

# A KNOWN_FAIL entry no test ever matched is stale. Only checkable on a full
# run: `run_tests.sh pass` legitimately never reaches a test/*.sf entry.
if [[ "$RUN_MAIN" == true && "$RUN_PASS" == true && "$RUN_FAIL" == true ]]; then
    KF_STALE=()
    while read -r l bug; do
        [[ -z "$l" ]] && continue
        grep -qxF "$l" "$KF_SEEN" || KF_STALE+=("$l|$bug")
    done <<<"$KNOWN_FAIL"
    if [[ ${#KF_STALE[@]} -gt 0 ]]; then
        echo ""
        echo "=== Stale KNOWN_FAIL entries (no such test ran — fix or drop them) ==="
        for entry in "${KF_STALE[@]}"; do
            IFS='|' read -r l bug <<<"$entry"
            printf '  BUGS #%-4s %s  — never matched a test; renamed, deleted, or skipped?\n' "$bug" "$l"
            FAILURES+=("stale-known-fail|$l|BUGS #$bug entry matched no test")
            TOTAL_FAIL=$((TOTAL_FAIL + 1))
        done
    fi
fi

if [[ ${#XFAILS[@]} -gt 0 ]]; then
    echo ""
    echo "=== Known failures (open bugs — expected red, not counted as failures) ==="
    for entry in "${XFAILS[@]}"; do
        IFS='|' read -r name bug detail <<<"$entry"
        printf '  BUGS #%-4s %s%s\n' "$bug" "$name" "${detail:+  — $detail}"
    done
fi

if [[ ${#FAILURES[@]} -gt 0 ]]; then
    echo ""
    echo "=== Failures ==="
    for entry in "${FAILURES[@]}"; do
        IFS='|' read -r cat name detail <<<"$entry"
        printf '  %-18s %s%s\n' "$cat" "$name" "${detail:+  — $detail}"
    done
fi

echo ""
XF_NOTE=""
[[ $TOTAL_XFAIL -gt 0 ]] && XF_NOTE=", $TOTAL_XFAIL known-fail"
echo "TOTAL: $TOTAL_PASS passed, $TOTAL_FAIL failed, $TOTAL_SKIP skipped$XF_NOTE"
[[ $TOTAL_FAIL -eq 0 ]]
