#!/usr/bin/env python3
"""Mechanically widen every site of an AST node by appending one binding/argument.

Usage: widen_sweep.py NodeName NewArgSpelling [--expect OLD_ARITY]

For each site found by the same paren-balancing scanner arity_check.py uses, insert
`, <NewArgSpelling>` immediately before the site's closing paren. Sites already at
OLD_ARITY+1 are left alone (idempotent). Pattern arms and constructions are treated
identically, which is exactly what we want: both need one more slot.

`--arg-for` lets construction sites take a different spelling than match arms, e.g.
a literal `"public"` at construction versus a binding name in a pattern. Sites in
files listed after --ctor are treated as constructions.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from arity_check import ROOT, tracked_files, strip_noise, split_args, EXCLUDE_QUALIFIERS


def find_sites(node, path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as fh:
        src = fh.read()
    masked = strip_noise(src)
    out = []
    qpat = re.compile(r"(?<![A-Za-z0-9_])(?:([A-Za-z_][A-Za-z0-9_]*)\.)?" + re.escape(node) + r"\s*\(")
    for m in qpat.finditer(masked):
        if m.group(1) in EXCLUDE_QUALIFIERS:
            continue
        open_idx = m.end() - 1
        depth = 0
        j = open_idx
        while j < len(masked):
            if masked[j] == "(":
                depth += 1
            elif masked[j] == ")":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        nargs = split_args(masked, open_idx + 1, j)
        out.append((m.start(), open_idx, j, nargs))
    return src, out


def main():
    node = sys.argv[1]
    spelling = sys.argv[2]
    expect = None
    if "--expect" in sys.argv:
        expect = int(sys.argv[sys.argv.index("--expect") + 1])
    skip = set()
    if "--skip" in sys.argv:
        skip = set(sys.argv[sys.argv.index("--skip") + 1].split(","))

    changed = 0
    for path in tracked_files():
        if path in skip:
            continue
        src, found = find_sites(node, path)
        if not found:
            continue
        # apply right-to-left so earlier offsets stay valid
        newsrc = src
        n_here = 0
        for (_st, _oi, close, nargs) in sorted(found, key=lambda t: -t[2]):
            if expect is not None and nargs != expect:
                continue
            newsrc = newsrc[:close] + ", " + spelling + newsrc[close:]
            n_here += 1
        if n_here:
            with open(os.path.join(ROOT, path), "w", encoding="utf-8") as fh:
                fh.write(newsrc)
            print(f"{path}: {n_here}")
            changed += n_here
    print(f"total widened: {changed}")


if __name__ == "__main__":
    main()
