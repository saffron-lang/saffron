#!/usr/bin/env python3
"""Find import aliases that bind to two different FILES inside one compilation.

Tree-wide counting over-reports: `Parser` means src/compiler/parser.sf in
main.sf and src/lib/parser.sf in gen_docs.sf, but those are separate programs
and never share an alias table. A collision only matters when both imports are
reachable from the same entry point, so this walks the real import graph.

Why it matters: main.sf:924 is `if (!all_aliases.has(ma))`. First writer wins,
later ones are dropped in silence. The program compiles, links, exits 0 and
calls the wrong module's function.
"""
import re, sys, pathlib, collections

ROOT = pathlib.Path(__file__).resolve().parent.parent
IMPORT = re.compile(r'\s*import\s+(?:\{[^}]*\}\s+from\s+|)"([^"]+)"(?:\s+as\s+([A-Za-z_]\w*))?\s*$')


def resolve(src: pathlib.Path, path: str):
    if path.startswith("@"):
        return ROOT / "src/lib" / (path[1:] + ".sf")
    if path.startswith("."):
        return (src.parent / path).resolve()
    cand = ROOT / "src/lib" / (path + ".sf")
    return cand if cand.exists() else None      # builtin C module


def imports_of(f: pathlib.Path):
    out = []
    try:
        text = f.read_text()
    except OSError:
        return out
    for i, line in enumerate(text.split("\n"), 1):
        if not line.lstrip().startswith("import"):
            continue
        m = IMPORT.match(line)
        if m:
            out.append((i, m.group(1), m.group(2)))
    return out


def closure(entry: pathlib.Path):
    seen, stack = set(), [entry]
    while stack:
        f = stack.pop()
        if f in seen or not f.exists():
            continue
        seen.add(f)
        for _, path, _alias in imports_of(f):
            t = resolve(f, path)
            if t is not None:
                stack.append(t)
    return seen


def main():
    entries = sorted(set(
        list(ROOT.glob("tools/*.sf")) + list(ROOT.glob("src/compiler/main.sf")) +
        list(ROOT.glob("src/lib/*.sf")) + list(ROOT.glob("test/*.sf"))
    ))
    bad = 0
    for entry in entries:
        if "src/lib/llvm" in str(entry):
            continue
        alias_to = collections.defaultdict(set)
        where = collections.defaultdict(list)
        for f in closure(entry):
            for ln, path, alias in imports_of(f):
                if not alias:
                    continue
                t = resolve(f, path)
                if t is None or not t.exists():
                    continue
                alias_to[alias].add(t)
                where[alias].append((f, ln, path, t))
        for alias, targets in sorted(alias_to.items()):
            if len(targets) < 2:
                continue
            bad += 1
            print(f"CONFLICT in closure of {entry.relative_to(ROOT)}: alias '{alias}' -> {len(targets)} files")
            for f, ln, path, t in where[alias]:
                print(f"    {f.relative_to(ROOT)}:{ln}  \"{path}\"  ->  {t.relative_to(ROOT)}")
    print(f"\ntotal conflicting (entry, alias) pairs: {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
