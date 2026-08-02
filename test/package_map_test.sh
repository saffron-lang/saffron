#!/usr/bin/env bash
# package_map_test.sh — verify the compiler's file → owning-package mapping.
#
# The mapping (main.sf: record_file_package / package_root_of_file /
# same_package) has no surface syntax and nothing consumes it yet, so it is not
# observable from a Saffron program. It is therefore NOT testable as a
# test/pass/*.sf file, and pretending otherwise would be a test of nothing. The
# compiler's --dump-packages flag exists to make it observable, and this script
# drives that flag.
#
# Not run by tools/run_tests.sh (which globs *.sf); run it directly:
#   test/package_map_test.sh

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAFFRONC="${SAFFRONC:-$ROOT/build/saffronc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

[[ -x "$SAFFRONC" ]] || { echo "saffronc not found at $SAFFRONC — run ./bootstrap.sh"; exit 1; }

PASS=0
FAIL=0

# Print the mapping row for one file. Column 1 is the loaded path, column 2 the
# owning package root, column 3 the declared [package] name.
dump_row() {
    local src="$1"
    "$SAFFRONC" --dump-packages --stdlib "$ROOT/src/lib" "$src" "$TMP/out.ll" 2>/dev/null \
        | grep -v '^REL	' | grep -F "$(basename "$src")" | head -1
}

# The REL rows report same_module_file / same_package between the first loaded
# file (the entry) and every other loaded file.
dump_rel() {
    local src="$1"
    "$SAFFRONC" --dump-packages --stdlib "$ROOT/src/lib" "$src" "$TMP/out.ll" 2>/dev/null \
        | grep '^REL	'
}

check() {
    local label="$1" src="$2" want_root="$3" want_name="$4"
    local row root name
    row="$(dump_row "$src")"
    root="$(printf '%s' "$row" | cut -f2)"
    name="$(printf '%s' "$row" | cut -f3)"
    if [[ "$root" == "$want_root" && "$name" == "$want_name" ]]; then
        echo "ok    $label"
        PASS=$((PASS + 1))
    else
        echo "FAIL  $label"
        echo "        want root='$want_root' name='$want_name'"
        echo "        got  root='$root' name='$name'"
        FAIL=$((FAIL + 1))
    fi
}

echo "--- file → owning package ---"

# Case 1: a file in the root package. src/lib/ has no pantry.toml of its own, so
# the nearest manifest above it is the repo root's.
printf 'IO.println("root pkg")\n' > "$ROOT/test/_pkgmap_root.sf"
check "root package" "$ROOT/test/_pkgmap_root.sf" "$ROOT" "saffron"
rm -f "$ROOT/test/_pkgmap_root.sf"

# Case 2: a file in a NESTED package. bazaar/pantry.toml (name "bazaar") and
# bazaar/frontend/pantry.toml (name "bazaar-frontend") both exist; nearest-above
# means a file under frontend/ belongs to the inner one.
printf 'IO.println("nested pkg")\n' > "$ROOT/bazaar/frontend/_pkgmap_nested.sf"
check "nested package (inner wins)" "$ROOT/bazaar/frontend/_pkgmap_nested.sf" \
    "$ROOT/bazaar/frontend" "bazaar-frontend"
rm -f "$ROOT/bazaar/frontend/_pkgmap_nested.sf"

# Case 3: a file with NO owning package. Nothing inside this repo qualifies —
# the repo root has a pantry.toml, so every file in the tree inherits it. A file
# outside any manifest's subtree is the real case, and "no package" must read as
# a distinct value, never as an empty string or an invented name.
printf 'IO.println("no pkg")\n' > "$TMP/orphan.sf"
check "no owning package" "$TMP/orphan.sf" "NO-PACKAGE" "NO-PACKAGE"

# Case 4: two packages sharing a declared name are still distinct packages.
# The repo root and src/compiler/ both declare name = "saffron", so identity
# must be the manifest directory, not the name.
echo "--- same declared name, different package ---"
root_row_root="$(dump_row "$ROOT/src/lib/os.sf" | cut -f2)"
comp_row_root="$(dump_row "$ROOT/src/compiler/lexer.sf" | cut -f2)"
if [[ -n "$root_row_root" && -n "$comp_row_root" && "$root_row_root" != "$comp_row_root" ]]; then
    echo "ok    root 'saffron' and src/compiler 'saffron' have distinct roots"
    PASS=$((PASS + 1))
else
    echo "FAIL  root and src/compiler collapsed to one package"
    echo "        root='$root_row_root' compiler='$comp_row_root'"
    FAIL=$((FAIL + 1))
fi

# The two relational queries a visibility check will call.
echo "--- same_module_file / same_package ---"

expect_rel() {
    local label="$1" rel="$2" other="$3" want_file="$4" want_pkg="$5"
    local row got_file got_pkg
    # Column 3 is the other file's path as the compiler loaded it, which is
    # absolute or relative depending on how --stdlib and the input were spelled.
    # Match on the tail so the assertion does not depend on that spelling.
    row="$(printf '%s\n' "$rel" | awk -F'\t' -v want="$other" \
        'index($3, want) && (index($3, want) + length(want) - 1) == length($3)' | head -1)"
    if [[ -z "$row" ]]; then
        echo "FAIL  $label — no REL row mentioning $other"
        FAIL=$((FAIL + 1))
        return
    fi
    got_file="$(printf '%s' "$row" | cut -f4)"
    got_pkg="$(printf '%s' "$row" | cut -f5)"
    if [[ "$got_file" == "same_file=$want_file" && "$got_pkg" == "same_pkg=$want_pkg" ]]; then
        echo "ok    $label"
        PASS=$((PASS + 1))
    else
        echo "FAIL  $label"
        echo "        want same_file=$want_file same_pkg=$want_pkg"
        echo "        got  $got_file $got_pkg"
        FAIL=$((FAIL + 1))
    fi
}

# Entry inside the root package: itself is same-file and same-package; a stdlib
# file it imports is a different file but the SAME package (both under the repo
# root's manifest, no nearer one).
printf 'import "@os" as OS\nIO.println(OS.platform())\n' > "$ROOT/test/_pkgmap_rel.sf"
REL_ROOT="$(dump_rel "$ROOT/test/_pkgmap_rel.sf")"
expect_rel "entry vs itself"            "$REL_ROOT" "$ROOT/test/_pkgmap_rel.sf" true  true
expect_rel "entry vs same-package file" "$REL_ROOT" "src/lib/os.sf"             false true
rm -f "$ROOT/test/_pkgmap_rel.sf"

# A packageless entry: same-file against itself is true, but same_package must be
# FALSE even against itself — two files with no owning package do not share one.
# This is the I2 requirement: "no package" must not behave like a package.
REL_ORPHAN="$(dump_rel "$TMP/orphan.sf")"
expect_rel "packageless entry vs itself" "$REL_ORPHAN" "$TMP/orphan.sf" true false
# ...and it is not in the root package either, though it imports from it.
expect_rel "packageless entry vs stdlib" "$REL_ORPHAN" "src/lib/prelude.sf" false false

echo ""
echo "TOTAL: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]] || exit 1
