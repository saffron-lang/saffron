#!/usr/bin/env bash
# BUGS #122 follow-on: members that EXIST under a module alias but are not
# assignable. These reach the same gen_set_field fall-through as an absent
# member, so they get the same invalid IR — the member-lookup miss is not the
# only way in.
set -eu
D="$(cd "$(dirname "$0")" && pwd)/b122_nonassign"
mkdir -p "$D"
rm -f "$D"/*.sf

w() { name="$1"; shift; printf '%s\n' "$@" > "$D/$name.sf"; }

w n01_assign_to_function   'import "@math" as Math' 'Math.abs = 1'
w n02_assign_to_function2  'import "@iter" as Iter' 'Iter.sum = 1'
w n03_assign_to_fn_in_fun  'import "@math" as Math' 'fun f() { Math.abs = 1 }' 'f()'
w n04_assign_to_builtin    'IO.println = 1'
w n05_assign_to_gc         'GC.collect = 1'
ls "$D"
