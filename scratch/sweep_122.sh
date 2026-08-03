#!/usr/bin/env bash
# BUGS #122 measurement sweep: assignment through a module alias.
#
# For every fixture in scratch/b122/, report exit code, whether a diagnostic
# appeared, whether the emitted IR loads from the bare alias, and whether opt
# accepts the module.
#
# Never puts ".sf" in an output filename (BUGS #119) and never pipes the
# compiler through anything, so $? is the compiler's own exit code.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SC="${SC:-$ROOT/build/saffronc}"
OPT="${OPT:-/opt/homebrew/opt/llvm/bin/opt}"
DIR="${1:-$ROOT/scratch/b122}"
OUTDIR="$(mktemp -d)"

printf '%-34s %4s %-9s %-7s %s\n' FIXTURE EXIT DIAG ALIASLD OPT
for f in "$DIR"/*.sf; do
    name="$(basename "$f" .sf)"
    ll="$OUTDIR/$name.ll"
    log="$OUTDIR/$name.log"
    "$SC" "$f" "$ll" >"$log" 2>&1
    rc=$?
    diag=no
    grep -q "no member" "$log" && diag=member
    grep -q "cannot assign to" "$log" && diag=notvar
    if [ "$diag" = no ] && grep -qi "error" "$log"; then diag=other; fi
    aliasld=no
    # A load from an SSA name that is a bare capitalised alias, e.g. `%Math`.
    if [ -f "$ll" ] && grep -qE '= load i64, i64\* %[A-Z][A-Za-z0-9_]*$' "$ll"; then
        aliasld=YES
    fi
    optv=n/a
    if [ -f "$ll" ]; then
        if "$OPT" -passes=verify "$ll" -o /dev/null >"$log.opt" 2>&1; then
            optv=ok
        else
            optv=REJECT
        fi
    fi
    printf '%-34s %4d %-9s %-7s %s\n' "$name" "$rc" "$diag" "$aliasld" "$optv"
done
echo "artifacts: $OUTDIR"
