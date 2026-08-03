#!/usr/bin/env bash
# BUGS #122 follow-on, part 2: writes through an alias to a member that exists
# but is not a variable — a function, a class, an enum, an enum variant.
set -eu
D="$(cd "$(dirname "$0")" && pwd)/b122_nonassign2"
mkdir -p "$D"
rm -f "$D"/*.sf
mkdir -p "$D/mod"

cat > "$D/mod/inner.sf" <<'EOF'
var counter: Int = 0
fun tick(): Int { return 1 }
class Widget { var w: Int
  fun init() { this.w = 0 } }
enum Colour { Red, Green }
EOF

w() { name="$1"; shift; printf '%s\n' "$@" > "$D/$name.sf"; }
I='import "./mod/inner.sf" as Inner'

w m01_assign_class     "$I" 'Inner.Widget = 1'
w m02_assign_enum      "$I" 'Inner.Colour = 1'
w m03_assign_variant   "$I" 'Inner.Red = 1'
w m04_assign_function  "$I" 'Inner.tick = 1'
w m05_assign_global_ok "$I" 'Inner.counter = 5' 'IO.println(Inner.counter)'
w m06_assign_missing   "$I" 'Inner.nope = 1'
ls "$D"
