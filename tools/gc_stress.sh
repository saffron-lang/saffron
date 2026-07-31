#!/bin/bash
# GC stress harness: run a Saffron program with a deliberately tiny nursery so
# that a minor collection happens almost continuously.
#
# Why this exists. Several GC defects (BUGS #63, #78) are latent under the
# default 256KB nursery: whether a collection lands in the middle of an
# expression depends on how much the program happened to allocate first, so the
# same program crashes, corrupts silently, or passes from run to run. That is
# the source of the suite's historical flakiness. Shrinking the nursery turns
# those latent hazards into deterministic crashes.
#
#   tools/gc_stress.sh prog.sf                    # default 4KB nursery
#   NURSERY=1024 tools/gc_stress.sh prog.sf       # more aggressive still
#   NURSERY=1073741824 tools/gc_stress.sh prog.sf # 1GB: the *control*. A
#                                                 # nursery that never fills, so
#                                                 # no minor collection ever
#                                                 # runs. A crash that vanishes
#                                                 # here but persists at 4KB is
#                                                 # the moving minor GC (#63);
#                                                 # one that survives both is a
#                                                 # separate codegen bug (#80).
#
# Native target only — every GC entry point is a no-op stub on wasm.
#
# Exit status is the program's own, so this drops straight into a test loop:
#
#   for f in test/pass/*.sf; do tools/gc_stress.sh "$f" >/dev/null 2>&1 \
#       || echo "FAIL $f"; done

set -u

if [ $# -lt 1 ]; then
    echo "usage: [NURSERY=<bytes>] $0 <program.sf> [args...]" >&2
    exit 2
fi

SRC="$1"
shift

if [ ! -f "$SRC" ]; then
    echo "$0: no such file: $SRC" >&2
    exit 2
fi

NURSERY="${NURSERY:-4096}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"

# The generated file has to live in the *source's* directory, not in the temp
# dir, or every relative `import "./x.sf"` in it would fail to resolve.
SRC_DIR="$(cd "$(dirname "$SRC")" && pwd)"
GEN="$SRC_DIR/.gc_stress_$$_$(basename "$SRC")"
trap 'rm -rf "$OUT"; rm -f "$GEN"' EXIT

# The nursery size is set from inside the program rather than by patching
# gc.ll, so this runs against the real committed collector — there is no forked
# copy to drift out of date. The call has to be the first thing that executes,
# before any allocation reaches the nursery.
{
    echo '@extern("void __gc_set_nursery_size(i64)") fun __gc_stress_nursery(bytes: Int)'
    echo "__gc_stress_nursery(${NURSERY})"
    cat "$SRC"
} > "$GEN"

if ! "$ROOT/tools/saffron" build "$GEN" -o "$OUT/prog" > "$OUT/build.log" 2>&1; then
    echo "$0: build failed for $SRC (nursery=${NURSERY})" >&2
    tail -20 "$OUT/build.log" >&2
    exit 1
fi

"$OUT/prog" "$@"
