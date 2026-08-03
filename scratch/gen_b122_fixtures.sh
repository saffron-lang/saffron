#!/usr/bin/env bash
# Generate the BUGS #122 fixture corpus under scratch/b122/.
set -eu
D="$(cd "$(dirname "$0")" && pwd)/b122"
mkdir -p "$D"
rm -f "$D"/*.sf

w() { name="$1"; shift; printf '%s\n' "$@" > "$D/$name.sf"; }

# --- rejection candidates: the member does not exist -------------------------
w 01_assign_toplevel      'import "@math" as Math' 'Math.NOPE = 3'
w 02_assign_in_fun        'import "@math" as Math' 'fun f() { Math.NOPE = 3 }' 'f()'
w 03_assign_both_missing  'import "@math" as Math' 'Math.NOPE = Math.ALSO_NOPE'
w 04_assign_value_valid   'import "@math" as Math' 'Math.NOPE = Math.abs(0 - 1)'
w 05_compound_plus        'import "@math" as Math' 'Math.NOPE += 1'
w 06_compound_minus       'import "@math" as Math' 'Math.NOPE -= 1'
w 07_compound_star        'import "@math" as Math' 'Math.NOPE *= 1'
w 08_compound_slash       'import "@math" as Math' 'Math.NOPE /= 1'
w 09_indexed_assign       'import "@math" as Math' 'Math.NOPE[0] = 1'
w 10_increment            'import "@math" as Math' 'Math.NOPE++'
w 11_nested_assign        'import "@math" as Math' 'Math.NOPE.deeper = 1'
w 12_assign_valid_index   'import "@math" as Math' 'Math.pi[0] = 1'

# --- reads, for comparison with #118 ----------------------------------------
w 17_read_missing         'import "@math" as Math' 'IO.println(Math.NOPE)'
w 18_read_valid           'import "@math" as Math' 'IO.println(Math.pi)'

# --- must keep working -------------------------------------------------------
w 13_valid_global_assign  'import "@math" as Math' 'Math.pi = 3.0' 'IO.println(Math.pi)'
w 14_valid_global_in_fun  'import "@math" as Math' \
                          'fun f(): Float { Math.pi = 3.0' '  return Math.pi }' \
                          'IO.println(f())'
w 15_class_field_assign   'class Box { var v: Int' '  fun init() { this.v = 0 } }' \
                          'var b = Box()' 'b.v = 7' 'IO.println(b.v)'
w 16_local_shadows_alias  'import "@math" as Math' 'fun f(): Int {' '  var Math: Int = 1' \
                          '  Math = 2' '  return Math }' 'IO.println(f())'
ls "$D"
