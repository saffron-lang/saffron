#!/bin/bash
# Sweep every test through gen3, looking only for the new resolver diagnostic
# and for link failures. Not a substitute for run_tests.sh (the coordinator runs
# that serially at merge time) — this only answers "did hardening the resolver
# tail turn working code into a compile error?"
cd /Users/willemhs/personal/saffron/.claude/worktrees/agent-a0d3a53e6203eb99b || exit 1
for f in test/*.sf test/pass/*.sf; do
    out=$(timeout 120 tools/saffron run "$f" 2>&1)
    if echo "$out" | grep -q "no symbol for method"; then
        echo "RESOLVER-DIAG  $f"
        echo "$out" | grep "no symbol for method" | head -2
    fi
    if echo "$out" | grep -q "use of undefined value\|Undefined symbols"; then
        echo "LINK-FAIL      $f"
        echo "$out" | grep -m1 "use of undefined value\|Undefined symbols"
    fi
done
echo "SWEEP DONE"
