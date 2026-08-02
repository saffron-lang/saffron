#!/usr/bin/env bash
# differential.sh — the differential test oracle.
#
# WHY THIS EXISTS
#
# Every remaining bug class in this compiler is a SILENT WRONG ANSWER. Not a
# crash, not a diagnostic. Two examples, both fixed the same week this script was
# written:
#
#   BUGS #102 — an enum's auto-generated to_string() decoded a one-field enum's
#     heap pointer as a `tag << 56` immediate, so EVERY value of such an enum
#     stringified as its first variant with the low 56 bits of the pointer as the
#     "payload". It compiled cleanly and had presumably been wrong for months.
#   BUGS #77 — on wasm32 only, __bool_to_string untagged an already-untagged
#     value, so every `true` printed as "false" while branching on the same value
#     stayed correct.
#
# Neither is findable by "does it compile". Neither is findable by a bootstrap:
# the compiler self-hosts in --identity-mode, where the tag/untag emitters are
# no-ops (src/compiler/codegen/types_body.sf), so the entire representation layer
# is bypassed when the compiler compiles itself. And neither is findable by
# tools/run_tests.sh on its own, because that script decides pass/fail from exit
# status plus optional in-program @test assertions — 64 of the 174 positive tests
# have neither assertions nor a .expected file and would pass on ANY output.
#
# The class needs an ORACLE: something that says what a program SHOULD output,
# independent of any one codegen path. This script provides the cheapest useful
# oracle, which needs no reference implementation at all:
#
#     THE SAME PROGRAM, COMPILED AND RUN TWO DIFFERENT WAYS, MUST PRODUCE
#     IDENTICAL STDOUT.
#
# Neither side is privileged. A disagreement does not say which one is wrong; it
# says one of them is, which is the finding. #77 would have been caught by this
# for free, on the first line of output, with no expected-output file authored
# and no reference interpreter written.
#
# CONFIGURATIONS
#
#   native-O2   clang -O2, base_nanbox.ll        (the reference: what users run)
#   native-O0   clang -O0, same runtime          (isolates optimizer-visible UB —
#                                                 a tagging bug that survives -O0
#                                                 and dies at -O2 is reading
#                                                 uninitialised or aliased memory)
#   wasm32      wasm_base_32.ll, run under node  (a genuinely separate runtime
#                                                 base and pointer width; this is
#                                                 the axis that catches #77-shaped
#                                                 bugs, because the four .ll bases
#                                                 are hand-maintained copies that
#                                                 drift — see compiler-rewrite.md
#                                                 M5)
#
# WHAT IT DELIBERATELY DOES NOT DO
#
# It does not try to run every test on every configuration. A narrow oracle that
# is trusted is worth far more than a broad one that cries wolf, so this script
# fails CLOSED in three ways:
#
#   1. Nondeterminism gate. The reference configuration is run TWICE and its two
#      outputs compared. A test that disagrees with itself cannot grade anything,
#      so it is reported as `nondet` and excluded. test/gc_deep_test.sf is exactly
#      this case and is caught automatically rather than by a hardcoded name.
#   2. Capability gate. A program whose imports need a host the wasm shim does not
#      provide (sockets, files, processes, clocks) is `skip-wasm`, not a failure.
#      The wasm_run.mjs Proxy resolves missing imports to `() => 0`, so such a
#      program would produce plausible-looking wrong output and be filed as a
#      compiler bug. See the import table below.
#   3. Build gate. A configuration that fails to BUILD is reported in its own
#      category (`build-fail-<config>`), never merged into mismatches. Not
#      compiling for wasm32 is a real and separate finding from computing the
#      wrong answer on wasm32.
#
# USAGE
#
#   tools/differential.sh                      all suites, all configurations
#   tools/differential.sh test/lists.sf ...    just these files
#   tools/differential.sh --suite pass         only test/pass/*.sf
#   tools/differential.sh --config native-O0   only this comparison config
#   tools/differential.sh --record             write missing .expected files from
#                                              the reference run (see below)
#   tools/differential.sh -v                   show the diff for every mismatch
#
# --record is how the second half of the oracle gets built. run_tests.sh already
# diffs `<name>.expected` against stdout when the file exists; the gap is that
# only 33 of 174 tests have one. --record captures the reference output for the
# tests that agree across all configurations, which is the only output worth
# freezing: a per-test expected file records what the program prints, so a future
# silent change becomes a test failure instead of a passing test. Recording an
# output the configurations already DISAGREE about would freeze a bug, so
# --record refuses to write for any test that is not unanimous.

set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAFFRON="$ROOT/tools/saffron"
SAFFRONC="${SAFFRONC:-$ROOT/build/saffronc}"
WASM_HOST="$ROOT/tools/oracle/wasm_run.mjs"

BUILD_TIMEOUT=${BUILD_TIMEOUT:-90}
RUN_TIMEOUT=${RUN_TIMEOUT:-15}

TMP=$(mktemp -d /tmp/saffron_diff_XXXXXX)
trap 'rm -rf "$TMP"' EXIT

# --- Configuration set -------------------------------------------------------

ALL_CONFIGS="native-O0 wasm32"
CONFIGS="$ALL_CONFIGS"

# --- Capability gate ---------------------------------------------------------
#
# Imports that need a host the wasm shim cannot supply. These are not compiler
# limitations, they are the shim's: wasm_run.mjs stubs unknown imports to
# `() => 0`, so a program that reads a file gets 0 bytes and prints something
# plausible and wrong. Grading that as a compiler bug would poison the oracle's
# credibility, which is the one thing it cannot afford.
#
# Kept as a literal import-string list rather than a heuristic so that adding a
# host function to the shim is a one-line deletion here, and so that the reason a
# test is skipped is auditable.
WASM_UNSUPPORTED_IMPORTS='"@net"|"@io"|"@os"|"os"|"@dns"|"@http/server"|"@http/client"|"@glob"|"time"|"@time"|"random"|"@random"|"@uuid"|"reflect"|"@reflect"|"@pantry_config"|"@log"'

# Async on wasm is a different scheduler (no coroutine lowering, no host event
# loop in the shim), so an async program's INTERLEAVING legitimately differs
# between native and wasm. That is a semantic difference by design, not a bug, so
# comparing their stdout line-for-line would report a false mismatch.
WASM_UNSUPPORTED_IMPORTS="$WASM_UNSUPPORTED_IMPORTS"'|"@async"|"@scheduler"|"@promise"|"@future"|"task"'

# GC tests inspect collector statistics. Every GC entry point is a no-op stub on
# wasm (see tools/gc_stress.sh), so their output is meaningless there.
wasm_unsupported_by_name() {
    case "$1" in
        gc_*|*_gc|test_gc*) return 0 ;;
        hello_wasm) return 0 ;;   # already wasm-only, nothing to compare against
        *) return 1 ;;
    esac
}

# IO.print (no trailing newline) is not expressible on wasm32. Both __io_println
# and __io_print land on the single js_log_str import (wasm_base_32.ll:1751),
# where native __io_print is printf("%s") with no terminator
# (base_nanbox.ll:113-119). The host therefore has to terminate every call, so a
# program that uses IO.print reads as extra newlines on the wasm side.
#
# That is a gap in the four hand-maintained .ll bases, not a wrong answer, and it
# was previously reported as a MISMATCH on test/pass/narrowing.sf — a harness
# artifact indistinguishable from a codegen bug. Skip by capability until a
# no-newline import exists in src/runtime/; then delete this function.
uses_io_print() {   # file
    grep -qE '\bIO\.print[[:space:]]*\(' "$1"
}

# A program whose entry point is `fun main()` with no top-level statements prints
# NOTHING on wasm32: output_body.sf emits the __saffron_boot shim only when the
# file has top-level code, while wasm_base_32.ll's _start calls @__saffron_boot()
# unconditionally and --import-undefined turns the missing symbol into a silent
# no-op. That is BUGS #110, a real and filed compiler bug, but it makes every such
# program a whole-output mismatch that drowns out any other finding, so it is
# gated by capability and tracked as #110 rather than re-reported per test.
main_entry_only() {   # file
    grep -qE '^[[:space:]]*fun[[:space:]]+main[[:space:]]*\(' "$1" || return 1
    # Any top-level call (e.g. `main()`, `IO.println(...)`) means the shim IS
    # emitted and the program runs normally on wasm32. Anchored at column 0 with
    # NO leading-whitespace allowance: an indented call sits inside a function
    # body, and allowing indentation made every `fun main` program look like it
    # had top-level code (mini_hello.sf matched on its own `IO.println` at line
    # 2). `fun main(...)` itself cannot match — the space after `fun` ends the
    # identifier before the '('.
    ! grep -qE '^[A-Za-z_][A-Za-z0-9_.]*[[:space:]]*\(' "$1"
}

# Tests that intentionally exit nonzero because their exit code IS the result
# (mini_while exits 55 = fib(10)). Their stdout is still comparable; only the
# exit-status check has to be relaxed.
exit_code_is_result() {
    case "$1" in mini_*) return 0 ;; *) return 1 ;; esac
}

# Files that are helpers for another test's `import "./x.sf"`, not tests. Running
# them standalone is meaningless and several print nothing at all.
is_helper() {
    case "$1" in *_helper) return 0 ;; *) return 1 ;; esac
}

# --- Options -----------------------------------------------------------------

VERBOSE=false
RECORD=false
SUITES="main pass"
EXPLICIT_FILES=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -v|--verbose) VERBOSE=true ;;
        --record) RECORD=true ;;
        --suite) shift; SUITES="$1" ;;
        --config) shift; CONFIGS="$1" ;;
        -h|--help) sed -n '2,120p' "$0"; exit 0 ;;
        *.sf) EXPLICIT_FILES+=("$1") ;;
        *) echo "differential.sh: unknown argument '$1'" >&2; exit 2 ;;
    esac
    shift
done

if [[ ! -x "$SAFFRONC" ]]; then
    echo "differential.sh: compiler not found at $SAFFRONC (build it with ./bootstrap.sh)" >&2
    exit 2
fi

WASM_AVAILABLE=true
if ! command -v node &>/dev/null; then
    WASM_AVAILABLE=false
    echo "differential.sh: node not found — the wasm32 configuration will be skipped" >&2
fi
if [[ ! -x /opt/homebrew/opt/llvm/bin/clang || ! -x /opt/homebrew/bin/wasm-ld ]]; then
    WASM_AVAILABLE=false
    echo "differential.sh: Homebrew LLVM (clang + wasm-ld) not found — wasm32 will be skipped" >&2
    echo "differential.sh: Apple's system clang cannot target wasm32 at all." >&2
fi

# --- Output normalization ----------------------------------------------------
#
# Compare through exactly one normalization, applied identically to both sides.
# Anything stripped here is a comparison the oracle gives up on, so the list is
# short and each entry has a reason.
#
#   [codegen]/[checker] Warning  — compiler chatter on stderr; not program output,
#                                  and the wasm build emits a different set of it
#                                  because it recompiles runtime.sf.
#
# Deliberately NOT normalized: float formatting, list/map rendering, pointer-ish
# values. Those are exactly where the wrong answers live. If the two backends
# format a float differently that is a finding, not noise to be filtered.
normalize() {
    grep -vE '^\[(codegen|checker)\] Warning' "$1" 2>/dev/null || true
}

# --- Per-configuration build + run -------------------------------------------
#
# Each returns the program's stdout+stderr in $OUT_FILE and its exit status in
# $RUN_EC, or sets BUILD_FAILED=1. Kept as one function per configuration rather
# than a parameterized builder so that a configuration's link line is readable in
# one place — the historical failure mode of this project's test tooling was a
# runner that reimplemented the link line and drifted from tools/saffron (see the
# note at the top of run_tests.sh), so every configuration below delegates.

build_and_run() {   # config file out_file  ->  sets RUN_EC / BUILD_FAILED
    local config="$1" f="$2" out="$3"
    local bin="$TMP/bin_$$"
    local blog="$TMP/build_$$.log"
    BUILD_FAILED=0
    RUN_EC=0

    case "$config" in
        native-O2)
            timeout "$BUILD_TIMEOUT" "$SAFFRON" build "$f" -o "$bin" >"$blog" 2>&1 \
                || { BUILD_FAILED=1; cp "$blog" "$out"; return; }
            timeout "$RUN_TIMEOUT" "$bin" >"$out" 2>&1 </dev/null
            RUN_EC=$?
            ;;
        native-O0)
            # -O0 keeps stores the optimizer would fold away. A tagging bug that
            # reads uninitialised memory often produces a *different* wrong answer
            # here than at -O2, and the disagreement is the signal.
            timeout "$BUILD_TIMEOUT" "$SAFFRON" build "$f" --opt 0 -o "$bin" >"$blog" 2>&1 \
                || { BUILD_FAILED=1; cp "$blog" "$out"; return; }
            timeout "$RUN_TIMEOUT" "$bin" >"$out" 2>&1 </dev/null
            RUN_EC=$?
            ;;
        wasm32)
            local wasm="$TMP/mod_$$.wasm"
            timeout "$BUILD_TIMEOUT" "$SAFFRON" build "$f" --target wasm32 -o "$wasm" >"$blog" 2>&1 \
                || { BUILD_FAILED=1; cp "$blog" "$out"; return; }
            timeout "$RUN_TIMEOUT" node "$WASM_HOST" "$wasm" >"$out" 2>&1 </dev/null
            RUN_EC=$?
            ;;
        *)
            echo "differential.sh: unknown configuration '$config'" >&2
            exit 2
            ;;
    esac
}

# --- Tallies -----------------------------------------------------------------

N_AGREE=0
N_MISMATCH=0
N_NONDET=0
N_SKIP=0
N_REFFAIL=0
N_BUILDFAIL=0
N_RECORDED=0

MISMATCHES=()
BUILDFAILS=()
NONDETS=()
REFFAILS=()

# --- Per-test driver ---------------------------------------------------------

check_file() {   # file label
    local f="$1" label="$2"
    local name; name=$(basename "$f" .sf)

    if is_helper "$name"; then
        printf 'SKIP     %-40s (import helper, not a standalone program)\n' "$label"
        N_SKIP=$((N_SKIP + 1))
        return
    fi

    # --- reference run, twice: determinism gate ---
    local ref1="$TMP/ref1" ref2="$TMP/ref2"
    build_and_run native-O2 "$f" "$ref1"
    if [[ $BUILD_FAILED -eq 1 ]]; then
        # The reference does not build. That is run_tests.sh's job to report, not
        # this script's: with no reference output there is nothing to differ
        # against, so counting it here would double-report a known failure.
        printf 'REF-FAIL %-40s reference config does not build\n' "$label"
        N_REFFAIL=$((N_REFFAIL + 1))
        REFFAILS+=("$label")
        return
    fi
    local ref_ec=$RUN_EC
    build_and_run native-O2 "$f" "$ref2"
    local ref_ec2=$RUN_EC

    if ! diff -q <(normalize "$ref1") <(normalize "$ref2") >/dev/null 2>&1 \
       || [[ "$ref_ec" != "$ref_ec2" ]]; then
        printf 'NONDET   %-40s reference disagrees with itself across two runs\n' "$label"
        N_NONDET=$((N_NONDET + 1))
        NONDETS+=("$label")
        return
    fi

    # A reference run that died is not a usable baseline either — its truncated
    # output would make every other configuration "mismatch" at the same point.
    if [[ $ref_ec -gt 128 ]] || { [[ $ref_ec -ne 0 ]] && ! exit_code_is_result "$name"; }; then
        printf 'REF-FAIL %-40s reference exited %d\n' "$label" "$ref_ec"
        N_REFFAIL=$((N_REFFAIL + 1))
        REFFAILS+=("$label ( exit $ref_ec )")
        return
    fi

    local unanimous=true
    local compared=0
    local cfg
    for cfg in $CONFIGS; do
        if [[ "$cfg" == "wasm32" ]]; then
            if [[ "$WASM_AVAILABLE" != true ]]; then continue; fi
            if wasm_unsupported_by_name "$name" \
               || grep -qE "import[^\"]*($WASM_UNSUPPORTED_IMPORTS)" "$f"; then
                printf 'SKIP     %-40s wasm32: needs a host the shim does not provide\n' "$label"
                N_SKIP=$((N_SKIP + 1))
                continue
            fi
            if uses_io_print "$f"; then
                printf 'SKIP     %-40s wasm32: IO.print has no no-newline import\n' "$label"
                N_SKIP=$((N_SKIP + 1))
                continue
            fi
            if main_entry_only "$f"; then
                printf 'SKIP     %-40s wasm32: `fun main` entry never runs (BUGS #110)\n' "$label"
                N_SKIP=$((N_SKIP + 1))
                continue
            fi
        fi

        local other="$TMP/other_$cfg"
        build_and_run "$cfg" "$f" "$other"
        if [[ $BUILD_FAILED -eq 1 ]]; then
            printf 'BUILDFAIL %-39s %s does not build\n' "$label" "$cfg"
            N_BUILDFAIL=$((N_BUILDFAIL + 1))
            BUILDFAILS+=("$label|$cfg|$(normalize "$other" | grep -m1 -iE 'error' | cut -c1-90)")
            unanimous=false
            continue
        fi

        local other_ec=$RUN_EC
        compared=$((compared + 1))

        # The exit status is part of the observable behaviour, and for one class of
        # test it is the ONLY part: test/mini_{1param,arithmetic,ifelse,while}.sf
        # print nothing and return their computed result as the process status.
        # Comparing stdout alone made those four AGREE by matching two empty files
        # — the oracle reproducing the exact blindness BUGS #107 describes, one
        # layer up. A wasm32 run that returned 0 where native returned 55 was a
        # unanimous pass.
        #
        # Deliberately not gated on exit_code_is_result: a status disagreement is a
        # real disagreement for any program. It is reported as a mismatch with the
        # statuses in the message, since a bare stdout diff would print nothing and
        # read as a spurious failure.
        if [[ "$ref_ec" != "$other_ec" ]]; then
            local ecmsg="exit status $ref_ec (native-O2) vs $other_ec ($cfg)"
            printf 'MISMATCH %-40s native-O2 != %s  — %s\n' "$label" "$cfg" "$ecmsg"
            N_MISMATCH=$((N_MISMATCH + 1))
            MISMATCHES+=("$label|$cfg|$ecmsg")
            unanimous=false
            continue
        fi

        if diff -q <(normalize "$ref1") <(normalize "$other") >/dev/null 2>&1; then
            printf 'AGREE    %-40s native-O2 == %s\n' "$label" "$cfg"
            N_AGREE=$((N_AGREE + 1))
        else
            local first
            first=$(diff <(normalize "$ref1") <(normalize "$other") | head -4 | tr '\n' ' ' | cut -c1-110)
            printf 'MISMATCH %-40s native-O2 != %s  — %s\n' "$label" "$cfg" "$first"
            N_MISMATCH=$((N_MISMATCH + 1))
            MISMATCHES+=("$label|$cfg|$first")
            unanimous=false
            if [[ "$VERBOSE" == true ]]; then
                echo "         --- native-O2 (left) vs $cfg (right) ---"
                diff <(normalize "$ref1") <(normalize "$other") | sed 's/^/         | /'
            fi
        fi
    done

    # --- --record: freeze the reference output as <name>.expected ---
    #
    # Only for a test that is unanimous across every configuration actually
    # compared. Freezing an output the backends disagree about would enshrine
    # whichever one this machine happened to run first as correct, which is worse
    # than having no expected file: it converts an open question into a confident
    # wrong answer.
    if [[ "$RECORD" == true ]]; then
        local expected="${f%.sf}.expected"
        if [[ -f "$expected" ]]; then
            :
        elif [[ "$unanimous" != true ]]; then
            printf '  no-record %-38s configurations disagree; refusing to freeze output\n' "$label"
        elif [[ $compared -eq 0 ]]; then
            printf '  no-record %-38s nothing was compared; reference output ungraded\n' "$label"
        else
            normalize "$ref1" >"$expected"
            printf '  recorded  %-38s -> %s\n' "$label" "$(basename "$expected")"
            N_RECORDED=$((N_RECORDED + 1))
        fi
    fi
}

# --- Drive -------------------------------------------------------------------

echo "=== differential oracle ==="
echo "reference:   native-O2"
echo "compared to: $CONFIGS"
[[ "$WASM_AVAILABLE" != true ]] && echo "             (wasm32 unavailable on this host)"
echo ""

if [[ ${#EXPLICIT_FILES[@]} -gt 0 ]]; then
    for f in "${EXPLICIT_FILES[@]}"; do
        [[ -f "$f" ]] || { echo "differential.sh: no such file: $f" >&2; exit 2; }
        check_file "$f" "$(basename "$f" .sf)"
    done
else
    for suite in $SUITES; do
        case "$suite" in
            main) glob="$ROOT/test/*.sf"; prefix="" ;;
            pass) glob="$ROOT/test/pass/*.sf"; prefix="pass/" ;;
            oracle) glob="$ROOT/test/oracle_*.sf"; prefix="" ;;
            *) echo "differential.sh: unknown suite '$suite'" >&2; exit 2 ;;
        esac
        echo "--- suite: $suite ---"
        for f in $glob; do
            [[ -f "$f" ]] || continue
            check_file "$f" "$prefix$(basename "$f" .sf)"
        done
        echo ""
    done
fi

# --- Summary -----------------------------------------------------------------

echo ""
echo "=== summary ==="
printf '  %-26s %d\n' "agreeing comparisons" "$N_AGREE"
printf '  %-26s %d\n' "MISMATCHES" "$N_MISMATCH"
printf '  %-26s %d\n' "build failures (non-ref)" "$N_BUILDFAIL"
printf '  %-26s %d\n' "nondeterministic (excluded)" "$N_NONDET"
printf '  %-26s %d\n' "skipped (capability gate)" "$N_SKIP"
printf '  %-26s %d\n' "reference failures" "$N_REFFAIL"
[[ "$RECORD" == true ]] && printf '  %-26s %d\n' ".expected files recorded" "$N_RECORDED"

if [[ ${#MISMATCHES[@]} -gt 0 ]]; then
    echo ""
    echo "=== MISMATCHES — one of the two configurations is producing a wrong answer ==="
    for entry in "${MISMATCHES[@]}"; do
        IFS='|' read -r label cfg detail <<<"$entry"
        printf '  %-34s native-O2 != %-10s %s\n' "$label" "$cfg" "$detail"
    done
fi

if [[ ${#BUILDFAILS[@]} -gt 0 ]]; then
    echo ""
    echo "=== build failures in a non-reference configuration ==="
    for entry in "${BUILDFAILS[@]}"; do
        IFS='|' read -r label cfg detail <<<"$entry"
        printf '  %-34s %-10s %s\n' "$label" "$cfg" "$detail"
    done
fi

if [[ ${#NONDETS[@]} -gt 0 ]]; then
    echo ""
    echo "=== nondeterministic: cannot grade, and cannot grade anything else either ==="
    for n in "${NONDETS[@]}"; do echo "  $n"; done
fi

if [[ ${#REFFAILS[@]} -gt 0 ]]; then
    echo ""
    echo "=== reference configuration failed (see tools/run_tests.sh for these) ==="
    for n in "${REFFAILS[@]}"; do echo "  $n"; done
fi

echo ""
# Exit nonzero only for mismatches. A build failure in a non-reference config and
# a nondeterministic test are both reported, loudly, but they are pre-existing
# conditions of the tree rather than a wrong answer this run discovered — making
# them fail the exit status would leave the script permanently red and therefore
# permanently ignored.
[[ $N_MISMATCH -eq 0 ]]
