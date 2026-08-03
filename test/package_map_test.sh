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

# ---------------------------------------------------------------------------
# The seam: module index → owning package, as the checker receives it
# ---------------------------------------------------------------------------
#
# Everything above tests the producer (main.sf's file → package mapping). These
# test the ENCODING that carries it to the checker, which is a separate thing
# that can be wrong on its own: package_roots_joined() newline-joins the roots
# indexed by the same i as prefixes_joined, and a checker that decodes a
# misaligned or short list would silently answer "different package" for every
# `internal` check — a denial that looks exactly like correct enforcement.
#
# The MOD rows are re-decoded from the joined string by the compiler itself, not
# read from the pre-encoding list, so an assertion here covers the encode/decode
# round trip and not just the map.

echo "--- seam: module index → package root ---"

dump_seam() {
    local src="$1"
    "$SAFFRONC" --dump-packages --stdlib "$ROOT/src/lib" "$src" "$TMP/out.ll" 2>/dev/null \
        | grep -E '^(MOD|COUNT|ENTRY)	'
}

# Lockstep is the invariant that matters most: module_file_paths is pushed
# alongside module_prefixes_list at three separate sites, and one missed push
# shifts every later module onto the wrong package.
check_counts() {
    local label="$1" seam="$2"
    local row mods prefixes roots
    row="$(printf '%s\n' "$seam" | grep '^COUNT' | head -1)"
    mods="$(printf '%s' "$row" | cut -f2 | cut -d= -f2)"
    prefixes="$(printf '%s' "$row" | cut -f3 | cut -d= -f2)"
    roots="$(printf '%s' "$row" | cut -f4 | cut -d= -f2)"
    if [[ -n "$mods" && "$mods" == "$prefixes" && "$mods" == "$roots" ]]; then
        echo "ok    $label (all three lists $mods long)"
        PASS=$((PASS + 1))
    else
        echo "FAIL  $label — lists out of lockstep"
        echo "        modules=$mods prefixes=$prefixes roots=$roots"
        FAIL=$((FAIL + 1))
    fi
}

# No module may encode as <MISSING>: that is the printer's marker for an index
# present in the prefix list but absent from the root list.
check_no_missing() {
    local label="$1" seam="$2"
    if printf '%s\n' "$seam" | grep -q '<MISSING>'; then
        echo "FAIL  $label — a module has no package entry"
        printf '%s\n' "$seam" | grep '<MISSING>' | sed 's/^/        /'
        FAIL=$((FAIL + 1))
    else
        echo "ok    $label"
        PASS=$((PASS + 1))
    fi
}

check_entry() {
    local label="$1" seam="$2" want="$3"
    local got
    got="$(printf '%s\n' "$seam" | grep '^ENTRY' | head -1 | cut -f3)"
    if [[ "$got" == "$want" ]]; then
        echo "ok    $label"
        PASS=$((PASS + 1))
    else
        echo "FAIL  $label"
        echo "        want '$want'  got '$got'"
        FAIL=$((FAIL + 1))
    fi
}

# Case A: an in-tree entry importing the stdlib. Every module is under the repo
# root's manifest, so every root is the repo root.
printf 'import "@iter" as Iter\nIO.println("seam")\n' > "$ROOT/test/_seam_root.sf"
SEAM_ROOT="$(dump_seam "$ROOT/test/_seam_root.sf")"
check_counts     "root-package program: lists in lockstep" "$SEAM_ROOT"
check_no_missing "root-package program: every module has a package" "$SEAM_ROOT"
check_entry      "root-package program: entry is the repo root" "$SEAM_ROOT" "$ROOT"
rm -f "$ROOT/test/_seam_root.sf"

# Case B: nearest-above governs per MODULE, not per program. An entry inside
# test/packages/testpkg/ pulls in that package's own modules AND the stdlib, so the same
# compile must report two different roots — this is the case a per-program
# "which package am I building?" shortcut would get wrong.
printf 'import "@iter" as Iter\nimport "./mod.sf" as Mod\nIO.println("seam")\n' \
    > "$ROOT/test/packages/testpkg/src/_seam_nested.sf"
SEAM_NESTED="$(dump_seam "$ROOT/test/packages/testpkg/src/_seam_nested.sf")"
check_counts     "nested-package program: lists in lockstep" "$SEAM_NESTED"
check_no_missing "nested-package program: every module has a package" "$SEAM_NESTED"
check_entry      "nested-package program: entry is the inner package" \
    "$SEAM_NESTED" "$ROOT/test/packages/testpkg"
if printf '%s\n' "$SEAM_NESTED" | grep -q "^MOD.*src/lib/iter.sf.*$ROOT\$" \
   && printf '%s\n' "$SEAM_NESTED" | grep -q "^MOD.*testpkg/src/mod.sf.*$ROOT/test/packages/testpkg\$"; then
    echo "ok    nested-package program: stdlib and package modules get different roots"
    PASS=$((PASS + 1))
else
    echo "FAIL  nested-package program: modules collapsed onto one root"
    printf '%s\n' "$SEAM_NESTED" | grep '^MOD' | sed 's/^/        /'
    FAIL=$((FAIL + 1))
fi
rm -f "$ROOT/test/packages/testpkg/src/_seam_nested.sf"

# Case C: a packageless entry still crosses the seam, as the marker. It must not
# inherit the stdlib's package just because it imports from it — that would grant
# `internal` access to every stdlib declaration from any script on the filesystem.
printf 'import "@iter" as Iter\nIO.println("seam")\n' > "$TMP/seam_orphan.sf"
SEAM_ORPHAN="$(dump_seam "$TMP/seam_orphan.sf")"
check_counts     "packageless program: lists in lockstep" "$SEAM_ORPHAN"
check_no_missing "packageless program: every module has an entry" "$SEAM_ORPHAN"
check_entry      "packageless entry encodes as the marker, not a package" \
    "$SEAM_ORPHAN" "nopkg"

echo ""
echo "TOTAL: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]] || exit 1
