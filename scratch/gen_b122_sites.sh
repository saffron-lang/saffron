#!/usr/bin/env bash
# BUGS #122 liveness sweep: try to reach the gen_expr SetField arm.
#
# `Math.NOPE = 3` in as many syntactic positions as the parser will build a
# SetField for, to establish which of the two lowering sites is live rather
# than reasoning about it. Same technique #118 used for its two read copies.
set -eu
D="$(cd "$(dirname "$0")" && pwd)/b122_sites"
mkdir -p "$D"
rm -f "$D"/*.sf

w() { name="$1"; shift; printf '%s\n' "$@" > "$D/$name.sf"; }
I='import "@math" as Math'

w s01_stmt_toplevel      "$I" 'Math.NOPE = 3'
w s02_stmt_in_fun        "$I" 'fun f() { Math.NOPE = 3 }' 'f()'
w s03_in_if              "$I" 'if (true) { Math.NOPE = 3 }'
w s04_in_while           "$I" 'var i: Int = 0' 'while (i < 1) { Math.NOPE = 3' '  i = i + 1 }'
w s05_in_for_in          "$I" 'for (x in [1]) { Math.NOPE = 3 }'
w s06_in_lambda_body     "$I" 'var g = fun () => 0' 'fun f() { Math.NOPE = 3 }' 'f()'
w s07_in_try             "$I" 'try { Math.NOPE = 3 } catch (e) { IO.println("x") }'
w s08_in_catch           "$I" 'try { throw "x" } catch (e) { Math.NOPE = 3 }'
w s09_in_finally         "$I" 'try { IO.println(1) } finally { Math.NOPE = 3 }'
w s10_in_match_arm       "$I" 'var v: Int = 1' 'var r = match (v) { _ => { Math.NOPE = 3 } }' 'IO.println(r)'
w s11_in_class_method    "$I" 'class C { var v: Int' '  fun init() { this.v = 0 }' \
                              '  fun go() { Math.NOPE = 3 } }' 'var c = C()' 'c.go()'
w s12_in_init            "$I" 'class C { var v: Int' '  fun init() { this.v = 0' \
                              '    Math.NOPE = 3 } }' 'var c = C()'
w s13_in_nested_fun      "$I" 'fun outer(): Int {' '  fun inner() { Math.NOPE = 3 }' \
                              '  inner()' '  return 0 }' 'IO.println(outer())'
w s14_in_else            "$I" 'if (false) { IO.println(1) } else { Math.NOPE = 3 }'
w s15_in_block           "$I" '{ Math.NOPE = 3 }'
w s16_two_in_a_row       "$I" 'Math.NOPE = 3' 'Math.ALSO = 4'
w s17_in_actor_method    "$I" 'actor A { var v: Int' '  fun init() { this.v = 0 }' \
                              '  fun go() { Math.NOPE = 3 } }' 'var a = A()' 'a.go()'
w s18_in_c_style_for     "$I" 'for (var i: Int = 0; i < 1; i = i + 1) { Math.NOPE = 3 }'
ls "$D"
