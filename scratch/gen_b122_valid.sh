#!/usr/bin/env bash
# BUGS #122 over-rejection sweep: everything that legitimately writes through
# something that LOOKS like a module alias must keep compiling.
#
# The #122 guard fires on `module_prefixes.has(name)` and a member-lookup miss.
# Every fixture here is a spelling where that condition could be met wrongly.
set -eu
D="$(cd "$(dirname "$0")" && pwd)/b122_valid"
mkdir -p "$D"
rm -f "$D"/*.sf

w() { name="$1"; shift; printf '%s\n' "$@" > "$D/$name.sf"; }

# Module global write through an alias — the one legitimate namespace write.
w v01_global_write        'import "@math" as Math' 'Math.pi = 3.0' 'IO.println(Math.pi)'
w v02_global_write_in_fun 'import "@math" as Math' 'fun f(): Float { Math.pi = 3.0' \
                          '  return Math.pi }' 'IO.println(f())'
w v03_global_write_twice  'import "@math" as Math' 'Math.pi = 3.0' 'Math.tau = 6.0' \
                          'IO.println(Math.pi + Math.tau)'
# A class instance field write — a completely different lowering path.
w v04_class_field         'class Box { var v: Int' '  fun init() { this.v = 0 } }' \
                          'var b = Box()' 'b.v = 7' 'IO.println(b.v)'
w v05_this_field          'class Box { var v: Int' '  fun init() { this.v = 1 }' \
                          '  fun set(n: Int) { this.v = n } }' 'var b = Box()' \
                          'b.set(9)' 'IO.println(b.v)'
w v06_nested_field        'class In { var n: Int' '  fun init() { this.n = 0 } }' \
                          'class Out { var i: In' '  fun init() { this.i = In() } }' \
                          'var o = Out()' 'o.i.n = 5' 'IO.println(o.i.n)'
# A local shadowing an alias: the write is on the local, not the module.
w v07_local_shadows       'import "@math" as Math' 'fun f(): Int {' '  var Math: Int = 1' \
                          '  Math = 2' '  return Math }' 'IO.println(f())'
w v08_class_named_as_alias 'import "@math" as Math' 'class Local { var v: Int' \
                          '  fun init() { this.v = 0 } }' 'var Math2 = Local()' \
                          'Math2.v = 4' 'IO.println(Math2.v)'
# A class instance held in a variable whose type came from a module.
w v09_module_class_field  'import "@test" as Test' 'class Box { var v: Int' \
                          '  fun init() { this.v = 0 } }' 'var b = Box()' 'b.v = 3' \
                          'Test.assert_eq(b.v, 3, "field write survives")' 'Test.summary()'
# Builtin namespaces are in module_prefixes too (IO/GC/Reflect); a *read* of a
# real member must not be disturbed, and no valid program writes to them.
w v10_builtin_read        'IO.println("ok")'
# Writing a module global from INSIDE the declaring module (current_prefix path).
w v11_own_global          'var g: Int = 1' 'fun bump() { g = g + 1 }' 'bump()' 'IO.println(g)'
# A module global holding a list, mutated through the alias (BUGS #118 noted this
# is the path that needed last_type published).
w v12_module_list_method  'import "@math" as Math' 'var l: List<Int> = []' 'l.push(1)' \
                          'IO.println(l.length() + Math.floor(1.2))'
ls "$D"
