#!/usr/bin/env bash
# json_output_test.sh — verify the compiler's --json structured output.
#
# --json is a driver mode, not surface syntax: it emits one JSON object
# ({file, diagnostics, symbols}) as the ONLY thing on stdout and writes no IR.
# That makes it untestable as a test/pass/*.sf file (there is nothing a Saffron
# program can observe), so it is driven from a shell harness the same way
# package_map_test.sh drives --dump-packages.
#
# The assertions that matter are byte-level, not line/col: every symbol offset
# and every located diagnostic offset must slice the ORIGINAL source back to the
# exact identifier. A line/col that looks plausible can still be one byte off; a
# slice cannot lie. python3 does the slicing and the JSON validity check
# (jq is not assumed present; python3 is already a test dependency).
#
# The compiler's JSON is captured to a file and python reads that file — NOT
# piped to python's stdin, because these checks use a heredoc for the python
# body and a heredoc already occupies stdin.
#
# Not run by tools/run_tests.sh (which globs *.sf); run it directly:
#   test/json_output_test.sh

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAFFRONC="${SAFFRONC:-$ROOT/build/saffronc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

[[ -x "$SAFFRONC" ]] || { echo "saffronc not found at $SAFFRONC — run ./bootstrap.sh"; exit 1; }
command -v python3 >/dev/null || { echo "python3 required for json_output_test.sh"; exit 1; }

PASS=0
FAIL=0

# Compile in --json mode and capture stdout to $TMP/out.json. The path is
# exported as JSON_FILE for the python bodies below.
JSON_FILE="$TMP/out.json"
export JSON_FILE
run_json() {
    "$SAFFRONC" --json --stdlib "$ROOT/src/lib" "$1" /dev/null > "$JSON_FILE" 2>&1
}

# --- Every symbol and located diagnostic slices back to its own identifier ----
#
# One fixture with each declaration kind (variable, function, enum + variants,
# class + methods) plus a deliberate type error on a declaration, so the checker
# error is LOCATED — the whole point of routing declaration spans (WS1a) through
# the sink. python3 validates JSON, checks the symbol kinds present, and slices.
cat > "$TMP/sample.sf" <<'SF'
var greeting: String = "hello"

fun add(a: Number, b: Number): Number {
    return a + b
}

enum Shape {
    Circle(r: Number),
    Square(side: Number)
}

class Point {
    var x: Number
    var y: Number
    fun init(x: Number, y: Number) { this.x = x; this.y = y }
    fun dist(): Number { return this.x + this.y }
}

var bad: Int = "not an int"
SF

run_json "$TMP/sample.sf"
SRC="$TMP/sample.sf" python3 - <<'PY'
import json, os, sys
src = open(os.environ["SRC"]).read()
try:
    data = json.load(open(os.environ["JSON_FILE"]))
except Exception as e:
    print("FAIL  sample: stdout is not valid JSON:", e); sys.exit(3)

failed = False
def ok(m):  print("ok   ", m)
def bad(m):
    global failed
    print("FAIL ", m); failed = True

if set(data.keys()) == {"file", "diagnostics", "symbols"}:
    ok("sample: object has exactly {file, diagnostics, symbols}")
else:
    bad(f"sample: unexpected keys {sorted(data.keys())}")

# Every symbol slices back to its own name.
mism = [s for s in data["symbols"] if src[s["offset"]:s["offset"]+s["length"]] != s["name"]]
if mism:
    for s in mism:
        bad(f"sample: symbol {s['name']!r} slices to {src[s['offset']:s['offset']+s['length']]!r}")
else:
    ok(f"sample: all {len(data['symbols'])} symbol offsets slice to their name")

# Every kind we expect appears, mapped from the right declaration.
kinds = {(s["name"], s["kind"]) for s in data["symbols"]}
want = {("greeting","variable"),("add","function"),("Shape","enum"),
        ("Circle","variant"),("Square","variant"),("Point","class"),
        ("init","method"),("dist","method"),("bad","variable")}
missing = want - kinds
if missing: bad(f"sample: missing symbols {sorted(missing)}")
else: ok("sample: every declaration kind is represented with the right kind")

# The type error is present, LOCATED, and its span slices to `bad`.
errs = [d for d in data["diagnostics"] if d["severity"] == "error"]
if len(errs) == 1 and errs[0]["phase"] == "checker":
    e = errs[0]
    if e.get("located") and src[e["offset"]:e["offset"]+e["length"]] == "bad":
        ok("sample: checker error is located and slices to `bad`")
    else:
        bad(f"sample: checker error not located at `bad`: {e}")
else:
    bad(f"sample: expected exactly one checker error, got {errs}")

sys.exit(1 if failed else 0)
PY
if [[ $? -eq 0 ]]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi

# --- stdout is JSON-only even on a parse error, and symbols parsed so far survive
printf 'var x = \nfun {\n' > "$TMP/parse_err.sf"
run_json "$TMP/parse_err.sf"
python3 - <<'PY'
import json, os, sys
try:
    data = json.load(open(os.environ["JSON_FILE"]))
except Exception as e:
    print("FAIL  parse_err: stdout is not valid JSON (a non-JSON diagnostic line leaked):", e)
    sys.exit(1)
if any(d["phase"] == "parser" for d in data["diagnostics"]):
    print("ok    parse_err: parser errors present as JSON")
else:
    print("FAIL  parse_err: no parser diagnostic"); sys.exit(1)
if any(s["name"] == "x" for s in data["symbols"]):
    print("ok    parse_err: symbols parsed before the failure are still emitted")
else:
    print("FAIL  parse_err: pre-failure symbol `x` missing"); sys.exit(1)
PY
if [[ $? -eq 0 ]]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi

# --- A clean file: empty diagnostics, exit 0, still valid JSON ----------------
printf 'var ok_var: Int = 3\n' > "$TMP/clean.sf"
run_json "$TMP/clean.sf"
CLEAN_RC=$?
python3 - <<'PY'
import json, os, sys
data = json.load(open(os.environ["JSON_FILE"]))
assert data["diagnostics"] == [], "clean file must have no diagnostics"
assert any(s["name"] == "ok_var" for s in data["symbols"]), "clean file symbol missing"
print("ok    clean: empty diagnostics, symbol present, valid JSON")
PY
if [[ $? -eq 0 ]]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
if [[ "$CLEAN_RC" -eq 0 ]]; then
    echo "ok    clean: --json exits 0 when there are no errors"
    PASS=$((PASS+1))
else
    echo "FAIL  clean: --json exited $CLEAN_RC on a clean file"
    FAIL=$((FAIL+1))
fi

# --- Escaping: a double-quote in the file path stays parseable ---------------
# The path reaches json_escape via the `file` field; without escaping this is
# the first thing to produce invalid JSON.
mkdir -p "$TMP/dq"
QPATH="$TMP/dq/a\"b.sf"
printf 'var q: Int = 1\n' > "$QPATH"
"$SAFFRONC" --json --stdlib "$ROOT/src/lib" "$QPATH" /dev/null > "$JSON_FILE" 2>&1
QPATH="$QPATH" python3 - <<'PY'
import json, os, sys
data = json.load(open(os.environ["JSON_FILE"]))  # raises if the quote broke the JSON
if data["file"] == os.environ["QPATH"]:
    print("ok    escape: a double-quote in the file path round-trips through JSON")
else:
    print("FAIL  escape: file path did not round-trip:", repr(data["file"])); sys.exit(1)
PY
if [[ $? -eq 0 ]]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi

# --- A missing input file is a JSON diagnostic, not a bare error line --------
"$SAFFRONC" --json --stdlib "$ROOT/src/lib" "$TMP/does_not_exist.sf" /dev/null > "$JSON_FILE" 2>&1
python3 - <<'PY'
import json, os, sys
data = json.load(open(os.environ["JSON_FILE"]))
if data["diagnostics"] and data["diagnostics"][0]["phase"] == "driver":
    print("ok    missing-file: reported as a JSON driver diagnostic")
else:
    print("FAIL  missing-file: not a driver diagnostic:", data["diagnostics"]); sys.exit(1)
PY
if [[ $? -eq 0 ]]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi

echo ""
echo "TOTAL: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]] || exit 1
