#!/usr/bin/env python3
"""Find every construction/pattern site of a named AST node and report its top-level
argument count, by balancing parentheses/brackets/braces and skipping strings and
comments. Multi-line sites are handled.

Usage: arity_check.py NodeName [NodeName...]
Scans tracked *.sf files (excluding build/) plus a few known non-.sf readers.
"""
import re
import subprocess
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# `src/lib/llvm/function.sf:7` declares `class Param` — an LLVM function parameter,
# entirely unrelated to `AST.Param`. It is spelled `Func.Param(` / `LLVMFunc.Param(`
# at use sites, and its own module spells it bare. Both are excluded so a sweep of
# the AST node does not widen the LLVM one.
EXCLUDE_FILES = {
    "src/lib/llvm/function.sf",
    "src/lib/llvm/module.sf",
    "src/lib/llvm/builder.sf",
    "src/lib/llvm/test_codegen.sf",
    "test/llvm_lib/test_module_builder.sf",
}
EXCLUDE_QUALIFIERS = {"Func", "LLVMFunc"}


def tracked_files():
    out = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode()
    files = [f for f in out.split("\0") if f]
    keep = []
    for f in files:
        if f.startswith("build/"):
            continue
        if f.startswith("legacy/"):
            continue
        if f in EXCLUDE_FILES:
            continue
        if f.endswith(".sf"):
            keep.append(f)
    return keep


def strip_noise(src):
    """Return src with string/char contents and comments replaced by spaces of the
    same length, so index arithmetic stays valid."""
    out = list(src)
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == '"':
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    out[j] = " "
                    if j + 1 < n:
                        out[j + 1] = " "
                    j += 2
                    continue
                if src[j] == '"':
                    break
                out[j] = " " if src[j] != "\n" else "\n"
                j += 1
            i = j + 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = i
            while j < n and src[j] != "\n":
                out[j] = " "
                j += 1
            i = j
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = i
            while j < n and not (src[j] == "*" and j + 1 < n and src[j + 1] == "/"):
                if src[j] != "\n":
                    out[j] = " "
                j += 1
            if j < n:
                out[j] = " "
                out[j + 1] = " "
                j += 2
            i = j
            continue
        i += 1
    return "".join(out)


def split_args(masked, start, end):
    """Count top-level comma-separated args in masked[start:end] (inside parens)."""
    depth = 0
    args = 0
    seen = False
    for k in range(start, end):
        ch = masked[k]
        if ch in "([{<":
            # '<' only counts as nesting for generics; conservatively ignore it
            if ch != "<":
                depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            args += 1
        if not ch.isspace():
            seen = True
    if not seen:
        return 0
    return args + 1


def sites(node, path):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as fh:
        src = fh.read()
    masked = strip_noise(src)
    res = []
    pat = re.compile(r"(?<![A-Za-z0-9_.])" + re.escape(node) + r"\s*\(")
    # also allow AST.Node( / Stmt.Node( qualified spellings
    qpat = re.compile(r"(?<![A-Za-z0-9_])(?:([A-Za-z_][A-Za-z0-9_]*)\.)?" + re.escape(node) + r"\s*\(")
    for m in qpat.finditer(masked):
        if m.group(1) in EXCLUDE_QUALIFIERS:
            continue
        open_idx = m.end() - 1
        depth = 0
        j = open_idx
        n = len(masked)
        while j < n:
            if masked[j] == "(":
                depth += 1
            elif masked[j] == ")":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        if j >= n:
            res.append((path, src[: m.start()].count("\n") + 1, None, "UNBALANCED"))
            continue
        nargs = split_args(masked, open_idx + 1, j)
        line = src[: m.start()].count("\n") + 1
        text = " ".join(src[m.start(): j + 1].split())
        if len(text) > 110:
            text = text[:107] + "..."
        res.append((path, line, nargs, text))
    return res


def main():
    nodes = sys.argv[1:]
    if not nodes:
        print(__doc__)
        return 1
    files = tracked_files()
    bad = 0
    for node in nodes:
        allsites = []
        for f in files:
            allsites.extend(sites(node, f))
        counts = {}
        for (_p, _l, n, _t) in allsites:
            counts[n] = counts.get(n, 0) + 1
        print(f"### {node}: {len(allsites)} sites; arity histogram: "
              + ", ".join(f"{k}->{v}" for k, v in sorted(counts.items(), key=lambda x: (x[0] is None, x[0]))))
        for (p, l, n, t) in allsites:
            print(f"  {n}  {p}:{l}  {t}")
    return bad


if __name__ == "__main__":
    sys.exit(main())
