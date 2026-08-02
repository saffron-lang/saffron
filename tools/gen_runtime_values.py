#!/usr/bin/env python3
"""Expand src/runtime/values.spec into the four LLVM IR runtime bases.

The NaN-box value layer (19 `__val_*` helpers) used to be hand-copied into four
`.ll` files. It drifted, and the drift shipped -- see BUGS #77, #82, #83, #39 and
the header comment of `values.spec`. This script makes the copies *generated*, so
that a helper defined once cannot disagree with itself across targets.

Usage:
    python3 tools/gen_runtime_values.py             rewrite the four bases in place
    python3 tools/gen_runtime_values.py --check     exit 1 if any base is stale
    python3 tools/gen_runtime_values.py --print wasm32
                                                    dump one target's block to stdout

Design notes (why it is shaped this way):

* Only the value layer is generated, not the whole base. Measured over the four
  files, 56 of 132 `define`s are common to all four and only 6 of those are
  textually identical; the rest of the overlap is wasm libc shims and GC
  forwarders whose differences are genuine. Generating everything would be a
  large machine for a small amount of real duplication. The value layer is where
  every drift bug to date has actually lived.

* Generation is *in place*, between markers, and the result is COMMITTED. The
  bootstrap has exactly one root of trust (`build/stage2/saffronc`) and
  `bootstrap.sh` consumes `base.ll` directly; making that file a build artifact
  would add a second root of trust and a Python dependency to the bootstrap. The
  generator is a developer tool and a CI gate, never a build step. `--check` is
  what keeps the committed output honest.

* This is not a second `sed` pipeline (design invariant I11). The `sed` assembly
  of `codegen/*_body.sf` is condemned because the file the compiler compiles is
  not a file anyone edits: `codegen.sf` is a build-time artifact and the `_body`
  mirrors silently diverge. Here the opposite holds -- the four `.ll` files stay
  the real, reviewable, committed inputs to the build, byte-for-byte diffable in
  review; the generator only refuses to let their value layers disagree.
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNTIME = os.path.join(ROOT, "src", "runtime")
SPEC = os.path.join(RUNTIME, "values.spec")

BEGIN = "; @generated-values:begin"
END = "; @generated-values:end"

BANNER = """{begin} -- DO NOT EDIT BELOW THIS LINE
; Generated from src/runtime/values.spec for target `{target}` (discipline:
; {discipline}) by tools/gen_runtime_values.py. Edit the spec, then re-run:
;
;     python3 tools/gen_runtime_values.py
;
; These 19 helpers are shared across four IR bases. They were hand-copied and
; drifted -- BUGS #77 had `true` printing as "false" on wasm32 for months because
; one base out of four untagged twice. Editing this block directly reintroduces
; exactly that failure mode, and `--check` in CI will fail."""


class SpecError(Exception):
    pass


def wrap_comment(text, width=78):
    """Wrap prose into `; `-prefixed LLVM comment lines."""
    words = text.split()
    lines = []
    cur = ";"
    for w in words:
        if cur == ";":
            cur = "; " + w
        elif len(cur) + 1 + len(w) <= width:
            cur += " " + w
        else:
            lines.append(cur)
            cur = ";   " + w
    if cur != ";":
        lines.append(cur)
    return "\n".join(lines)


class Target:
    def __init__(self, name, discipline, filename):
        self.name = name
        self.discipline = discipline
        self.filename = filename

    @property
    def path(self):
        return os.path.join(RUNTIME, self.filename)


class Helper:
    def __init__(self, name):
        self.name = name
        self.targets = []
        self.bodies = {}      # "identity" | "nanbox" | "both" -> body text
        self.overrides = {}   # target name -> (body text, reason)

    def body_for(self, target):
        """The body this helper contributes to `target`, or None if absent.

        An override's `reason` is emitted above the body as an LLVM comment. A
        reader of the `.ll` needs to know why this target's copy differs without
        having to go and find the spec, and it means the justification travels
        with the code the way the hand-written comment used to.
        """
        if target.name not in self.targets:
            return None
        if target.name in self.overrides:
            body, reason = self.overrides[target.name]
            return "%s\n%s" % (wrap_comment(
                "@override %s -- %s" % (target.name, reason)), body)
        if "both" in self.bodies:
            return self.bodies["both"]
        body = self.bodies.get(target.discipline)
        if body is None:
            raise SpecError(
                "helper %s lists target %s (discipline %s) but defines no "
                "@%s or @both body" % (self.name, target.name, target.discipline,
                                       target.discipline))
        return body


def parse_spec(path):
    """Parse values.spec into (targets, sections).

    sections is a list of (section_name, [Helper]).
    """
    with open(path) as fh:
        lines = fh.read().split("\n")

    targets = []
    sections = []
    helper = None
    section = None
    # Where body lines currently accumulate: ("kind", key) or None.
    sink = None
    buf = []
    pending_override = None

    def flush():
        nonlocal sink, buf, pending_override
        if sink is None:
            buf = []
            return
        text = "\n".join(buf).strip("\n")
        if not text:
            raise SpecError("empty body for helper %s (%s)"
                            % (helper.name if helper else "?", sink))
        kind, key = sink
        if kind == "body":
            if key in helper.bodies:
                raise SpecError("helper %s defines @%s twice" % (helper.name, key))
            helper.bodies[key] = text
        else:
            if pending_override is None:
                raise SpecError("@override %s for helper %s has no `reason =`"
                                % (key, helper.name))
            helper.overrides[key] = (text, pending_override)
            pending_override = None
        sink = None
        buf = []

    for lineno, raw in enumerate(lines, 1):
        line = raw.rstrip()
        stripped = line.strip()

        # `#` comments are spec-level prose and never reach the output. `;` is an
        # LLVM comment and IS part of a body, so it must not be filtered here.
        if stripped.startswith("#"):
            continue

        m = re.match(r"^@target\s+(\S+)\s+discipline=(\S+)\s+file=(\S+)$", stripped)
        if m:
            flush()
            targets.append(Target(m.group(1), m.group(2), m.group(3)))
            continue

        m = re.match(r"^\[section\s+(.*)\]$", stripped)
        if m:
            flush()
            helper = None
            section = (m.group(1), [])
            sections.append(section)
            continue

        m = re.match(r"^\[helper\s+(\S+)\]$", stripped)
        if m:
            flush()
            if section is None:
                raise SpecError("line %d: helper outside any [section]" % lineno)
            helper = Helper(m.group(1))
            section[1].append(helper)
            continue

        m = re.match(r"^targets\s*=\s*(.+)$", stripped)
        if m and helper is not None and sink is None:
            helper.targets = m.group(1).split()
            continue

        m = re.match(r"^@(identity|nanbox|both)$", stripped)
        if m:
            flush()
            if helper is None:
                raise SpecError("line %d: @%s outside a helper" % (lineno, m.group(1)))
            sink = ("body", m.group(1))
            continue

        m = re.match(r"^@override\s+(\S+)$", stripped)
        if m:
            flush()
            if helper is None:
                raise SpecError("line %d: @override outside a helper" % lineno)
            sink = ("override", m.group(1))
            continue

        m = re.match(r"^reason\s*=\s*(.+)$", stripped)
        if m and sink is not None and sink[0] == "override" and not buf:
            pending_override = m.group(1).strip()
            continue

        if sink is not None:
            buf.append(line)
        elif stripped:
            raise SpecError("line %d: unexpected content outside a body: %r"
                            % (lineno, stripped))

    flush()

    if not targets:
        raise SpecError("spec declares no @target lines")

    # Validation that would otherwise be a silent wrong answer.
    known = {t.name for t in targets}
    seen = set()
    for _, helpers in sections:
        for h in helpers:
            if h.name in seen:
                raise SpecError("helper %s declared twice" % h.name)
            seen.add(h.name)
            if not h.targets:
                raise SpecError("helper %s has no `targets =` line" % h.name)
            for t in h.targets:
                if t not in known:
                    raise SpecError("helper %s names unknown target %r"
                                    % (h.name, t))
            for t in h.overrides:
                if t not in h.targets:
                    raise SpecError("helper %s overrides %s, which is not in its "
                                    "targets" % (h.name, t))
            if "both" in h.bodies and ("identity" in h.bodies or "nanbox" in h.bodies):
                raise SpecError("helper %s mixes @both with a discipline body"
                                % h.name)
            # The define in the body must actually define the helper it claims to.
            for label, text in list(h.bodies.items()) + \
                    [(t, b) for t, (b, _) in h.overrides.items()]:
                got = re.search(r"^define\s+.*?@([A-Za-z0-9_.$]+)\s*\(", text, re.M)
                if not got:
                    raise SpecError("helper %s body (%s) has no `define`"
                                    % (h.name, label))
                if got.group(1) != h.name:
                    raise SpecError("helper %s body (%s) defines @%s instead"
                                    % (h.name, label, got.group(1)))
    return targets, sections


def render(target, sections):
    """Render the generated block for one target (no markers)."""
    out = []
    for name, helpers in sections:
        bodies = [h.body_for(target) for h in helpers]
        bodies = [b for b in bodies if b is not None]
        if not bodies:
            continue
        out.append("; --- %s ---" % name)
        out.append("")
        for body in bodies:
            out.append(body)
            out.append("")
    while out and out[-1] == "":
        out.pop()
    return "\n".join(out)


def splice(text, target, sections, declared):
    """Replace the marked region of `text`, or report that markers are missing."""
    lines = text.split("\n")
    begins = [i for i, l in enumerate(lines) if l.startswith(BEGIN)]
    ends = [i for i, l in enumerate(lines) if l.startswith(END)]
    if len(begins) != 1 or len(ends) != 1:
        raise SpecError(
            "%s must contain exactly one %r and one %r line (found %d and %d)"
            % (target.filename, BEGIN, END, len(begins), len(ends)))
    b, e = begins[0], ends[0]
    if b > e:
        raise SpecError("%s has the end marker before the begin marker"
                        % target.filename)

    # A `define` sitting inside the markers that the spec does not declare would
    # be DELETED by the rewrite below, silently and with no diff to review if the
    # writer is trusted. That is a worse failure than the drift this generator
    # exists to prevent -- drift is at least visible in a diff -- so refuse.
    #
    # This is not hypothetical: `__val_class_tag` and `__val_tag_ptr_nullable`
    # both landed inside the block while the spec knew nothing about them,
    # because the block is exactly where a reader looks for the value helpers.
    # Either declare it in values.spec, or move it below the end marker.
    inside = "\n".join(lines[b:e + 1])
    orphans = [name for name in
               re.findall(r"^define\s+.*?@([A-Za-z0-9_.$]+)\s*\(", inside, re.M)
               if name not in declared]
    if orphans:
        raise SpecError(
            "%s: the generated block defines %s, which values.spec does not "
            "declare.\nRegenerating would DELETE %s. Either add %s to "
            "values.spec, or move %s below the `%s` marker."
            % (target.filename, ", ".join("@" + o for o in orphans),
               "them" if len(orphans) > 1 else "it",
               "them" if len(orphans) > 1 else "it",
               "them" if len(orphans) > 1 else "it", END))

    banner = BANNER.format(begin=BEGIN, target=target.name,
                           discipline=target.discipline)
    block = [banner, ""] + render(target, sections).split("\n") + ["", END]
    return "\n".join(lines[:b] + block + lines[e + 1:])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="do not write; exit 1 if any base is out of date")
    ap.add_argument("--print", dest="dump", metavar="TARGET",
                    help="print one target's generated block and exit")
    args = ap.parse_args()

    try:
        targets, sections = parse_spec(SPEC)
    except SpecError as exc:
        sys.stderr.write("values.spec: %s\n" % exc)
        return 2

    if args.dump:
        match = [t for t in targets if t.name == args.dump]
        if not match:
            sys.stderr.write("unknown target %r (known: %s)\n"
                             % (args.dump, ", ".join(t.name for t in targets)))
            return 2
        print(render(match[0], sections))
        return 0

    declared = {h.name for _, helpers in sections for h in helpers}
    stale = []
    for target in targets:
        with open(target.path) as fh:
            old = fh.read()
        try:
            new = splice(old, target, sections, declared)
        except SpecError as exc:
            sys.stderr.write("%s\n" % exc)
            return 2
        if new == old:
            continue
        stale.append(target.filename)
        if not args.check:
            with open(target.path, "w") as fh:
                fh.write(new)

    n_helpers = sum(len(h) for _, h in sections)
    if args.check:
        if stale:
            sys.stderr.write(
                "src/runtime/values.spec and the IR bases disagree.\n"
                "Stale: %s\n"
                "Run: python3 tools/gen_runtime_values.py\n" % ", ".join(stale))
            return 1
        print("values.spec: %d helpers, %d targets, all bases up to date."
              % (n_helpers, len(targets)))
        return 0

    if stale:
        print("regenerated %s from values.spec (%d helpers)"
              % (", ".join(stale), n_helpers))
    else:
        print("no changes: all %d bases already match values.spec"
              % len(targets))
    return 0


if __name__ == "__main__":
    sys.exit(main())
