#!/usr/bin/env bash
# Derive the open-bug count and set from BUGS.md rather than trusting a
# hand-maintained tally. The stored "**N open entries: #...**" line in the
# header is the single worst source of merge conflicts in this repo: every
# branch that opens or closes an entry rewrites it, and concurrent worktrees
# then collide on that one line at merge. This reads the ground truth instead —
# the section an entry physically sits in.
#
# The rule the file already follows (CLAUDE.md, and BUGS.md's own preamble): an
# entry is OPEN iff its `### N.` heading appears between `## Open` and the FIRST
# `## Resolved`, and CLOSED once moved below it. That boundary is authoritative;
# a status WORD in the title is not, because early titles (### 2., ### 49.,
# ### 75.) carry none.
#
# Usage:
#   tools/bugs.sh            # print "N open: #a, #b, ..."
#   tools/bugs.sh --check    # also flag any FIXED-titled entry stranded in Open
#                            # (exit 1 if found) — the drift CLAUDE.md warns about
set -euo pipefail

FILE="${BUGS_FILE:-$(dirname "$0")/../BUGS.md}"

# Numbers of every `### N.` heading above the first `## Resolved`.
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
