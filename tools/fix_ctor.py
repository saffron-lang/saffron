#!/usr/bin/env python3
"""Replace a placeholder binding name with a literal at construction sites only.

Usage: fix_ctor.py file.sf PLACEHOLDER LITERAL line[,line...]

Only the given 1-based lines are touched, and the placeholder must be the last
argument of a call on that line. Fails loudly if it is not found.
"""
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    path, ph, lit, lines = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    targets = [int(x) for x in lines.split(",")]
    full = os.path.join(ROOT, path)
    src = open(full, encoding="utf-8").read().split("\n")
    for ln in targets:
        i = ln - 1
        old = src[i]
        needle = ", " + ph
        if needle not in old:
            raise SystemExit(f"{path}:{ln}: no '{needle}' in: {old}")
        # replace only the last occurrence on the line
        head, _, tail = old.rpartition(needle)
        src[i] = head + ", " + lit + tail
    open(full, "w", encoding="utf-8").write("\n".join(src))
    print(f"{path}: fixed {len(targets)} construction sites")


if __name__ == "__main__":
    main()
