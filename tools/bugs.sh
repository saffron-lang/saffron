#!/usr/bin/env bash
# Derive the open-bug count and set from BUGS.md rather than trusting a
# hand-maintained tally. The stored "**N open entries: #...**" line in the
# header is the single worst source of merge conflicts in this repo: every
# branch that opens or closes an entry rewrites it, and concurrent worktrees
# then collide on that one line at merge. This reads the ground truth instead —
# the file an entry physically sits in.
#
# The invariant, since the open/closed split: BUGS.md holds ONLY open entries,
# and the resolved/fixed archive lives in BUGS_CLOSED.md. So every `### N.`
# heading in BUGS.md is open by definition, and closing a bug MOVES its heading
# to the other file — a merge can no longer strand a FIXED entry in the open
# list, because the list is a whole file rather than a section above a marker.
# A status WORD in the title is still not authoritative (early titles ### 2.,
# ### 49., ### 75. carry none); the file is.
#
# The `^## Resolved$` guard below is kept as a belt-and-suspenders stop: if a
# stray Resolved section ever reappears in BUGS.md (a botched merge, a partial
# revert), headings under it are excluded rather than miscounted as open.
#
# Usage:
#   tools/bugs.sh            # print "N open: #a, #b, ..."
#   tools/bugs.sh --check    # also flag any FIXED-titled entry stranded in Open
#                            # (exit 1 if found) — the drift CLAUDE.md warns about
set -euo pipefail

FILE="${BUGS_FILE:-$(dirname "$0")/../BUGS.md}"

# Numbers of every `### N.` heading in the open file (stopping at a stray
# `## Resolved` if one ever leaks back in — see the header note).
open_nums=$(awk '
    /^## Resolved$/ { exit }
    /^### [0-9]+\./ { match($0, /[0-9]+/); print substr($0, RSTART, RLENGTH) }
' "$FILE")

count=$(printf '%s\n' "$open_nums" | grep -c . || true)
# Sort numerically so the set reads like the header did; paste's -d takes a
# cycling character LIST, so use awk to join with a literal ", ".
set_str=$(printf '%s\n' "$open_nums" | sort -n | sed 's/^/#/' \
    | awk 'NR==1{s=$0;next}{s=s", "$0}END{print s}')
echo "${count} open: ${set_str}"

if [[ "${1:-}" == "--check" ]]; then
    # Any entry whose own title says FIXED but which still sits in ## Open is
    # misfiled — it belongs under ## Resolved. This is the exact drift that once
    # left 81 entries in Open of which 14 were actually open.
    stranded=$(awk '
        /^## Resolved$/ { exit }
        /^### [0-9]+\. FIXED/ { print }
    ' "$FILE")
    if [[ -n "$stranded" ]]; then
        echo "ERROR: FIXED entries stranded in ## Open (move them below ## Resolved):" >&2
        printf '%s\n' "$stranded" >&2
        exit 1
    fi
fi
