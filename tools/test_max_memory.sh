#!/usr/bin/env bash
# test_max_memory.sh — verify the --max-memory / SAFFRON_MAX_MEMORY heap cap.
#
# This lives outside tools/run_tests.sh on purpose. The main suite decides
# pass/fail from the exit status (`record_fail "nonzero-exit"` in run_tests.sh),
# with the only escape hatch being the `mini_*` name prefix in
# exit_code_is_result(). A cap breach is *supposed* to exit 3, so a test that
# proves the cap works would be scored as a failure there. Rather than widen
# that allowlist for one feature, the exit-code assertions live here.
#
# Usage: tools/test_max_memory.sh
# Exits 0 if every case matches, 1 otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAFFRON="$ROOT/tools/saffron"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

PASS=0
FAIL=0

# A program whose live set genuinely exceeds a small cap. Doubling a string 30
# times reaches 1GiB, and does it through codegen-emitted malloc (string
# concatenation), which is the path that @__gc_total_bytes cannot see — so this
# also covers the reason the cap keeps its own counter.
cat >"$TMPDIR/overflow.sf" <<'EOF'
var s = "x"
for (i = 0; i < 30; i = i + 1) { s = s + s }
IO.println("len=${s.length()}")
EOF

# The same shape, but small enough to finish comfortably under any real cap.
cat >"$TMPDIR/modest.sf" <<'EOF'
var s = "x"
for (i = 0; i < 12; i = i + 1) { s = s + s }
IO.println("len=${s.length()}")
EOF

# expect <label> <expected-rc> <expected-substring-or-empty> -- <cmd...>
expect() {
    local label="$1" want_rc="$2" want_out="$3"; shift 3
    [[ "$1" == "--" ]] && shift
    local out rc
    out="$("$@" 2>&1)"; rc=$?
    if [[ "$rc" != "$want_rc" ]]; then
        printf 'FAIL  %-44s expected exit %s, got %s\n' "$label" "$want_rc" "$rc"
        printf '      output: %s\n' "$(tail -1 <<<"$out")"
        FAIL=$((FAIL + 1)); return
    fi
    if [[ -n "$want_out" && "$out" != *"$want_out"* ]]; then
        printf 'FAIL  %-44s exit %s ok, but output lacked %q\n' "$label" "$rc" "$want_out"
        printf '      output: %s\n' "$(tail -1 <<<"$out")"
        FAIL=$((FAIL + 1)); return
    fi
    printf 'PASS  %-44s exit %s\n' "$label" "$rc"
    PASS=$((PASS + 1))
}

echo "--- baseline: no cap ---"
expect "uncapped modest run" 0 "len=4096" -- \
    "$SAFFRON" run "$TMPDIR/modest.sf"

echo "--- cap not reached ---"
expect "--max-memory 64m, modest program" 0 "len=4096" -- \
    "$SAFFRON" run --max-memory 64m "$TMPDIR/modest.sf"

echo "--- cap breached (must exit 3, Java -XX:+ExitOnOutOfMemoryError) ---"
expect "--max-memory 16m, overflowing program" 3 "out of memory" -- \
    "$SAFFRON" run --max-memory 16m "$TMPDIR/overflow.sf"

expect "--max-memory=16m (equals form)" 3 "out of memory" -- \
    "$SAFFRON" run --max-memory=16m "$TMPDIR/overflow.sf"

echo "--- env var is equivalent to the flag ---"
expect "SAFFRON_MAX_MEMORY=16m" 3 "out of memory" -- \
    env SAFFRON_MAX_MEMORY=16m "$SAFFRON" run "$TMPDIR/overflow.sf"

echo "--- suffixes accepted (k/m/g, case-insensitive) ---"
for sz in 512k 512K 64m 64M 1g 1G 67108864; do
    expect "suffix '$sz' accepted" 0 "len=4096" -- \
        "$SAFFRON" run --max-memory "$sz" "$TMPDIR/modest.sf"
done

echo "--- malformed values rejected before compiling (exit 1, not 3) ---"
for bad in bogus 12x -1 '' 1.5m m; do
    expect "reject --max-memory '$bad'" 1 "invalid --max-memory value" -- \
        "$SAFFRON" run --max-memory "$bad" "$TMPDIR/modest.sf"
done

echo "--- a malformed env var is a usage error, not a silent unlimited run ---"
expect "SAFFRON_MAX_MEMORY=bogus" 1 "invalid SAFFRON_MAX_MEMORY" -- \
    env SAFFRON_MAX_MEMORY=bogus "$SAFFRON" run "$TMPDIR/modest.sf"

echo "--- flag is rejected where it would be silently ignored ---"
expect "--max-memory on 'build'" 1 "applies to 'saffron run'" -- \
    "$SAFFRON" build --max-memory 16m "$TMPDIR/modest.sf" -o "$TMPDIR/out.bin"

echo
echo "TOTAL: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
