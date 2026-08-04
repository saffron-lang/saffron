// Pure lexical helpers, in their own module so they can be tested without
// standing up an LSP connection. Importing server.ts runs it — it calls
// connection.listen() at load — so anything worth a unit test has to live
// outside it. These are exactly the functions whose bugs are silent: the
// scanner decides what rename rewrites.

// --- Identifier occurrences --------------------------------------------------
//
// References, rename, highlight and semantic tokens all need the same thing:
// every place an identifier appears *as code*. A bare regex over the buffer
// answers that wrongly — it matches inside comments and string literals, so
// renaming `count` would rewrite the word in a doc comment and inside
// `"count: 3"`, and highlight would light up prose.
//
// So this is a small scanner rather than a regex. It knows four things the
// regex cannot:
//
//   * `//`, `///` and `//!` run to end of line.
//   * `/* */` block comments NEST (lexer.sf:261) — a regex for the first `*/`
//     closes the outer comment early and then treats real code as commented.
//   * a string literal is not code...
//   * ...except inside `${...}`, which IS code and must be scanned. An
//     interpolated `"total ${count}"` genuinely references `count`, and a
//     rename that skipped it would silently break the program.
//
// This is lexical, not semantic: it finds occurrences of a *name*, not of a
// *binding*. The compiler cannot yet tell us which binding an occurrence
// resolves to (resolve.sf leaves member and method accesses unresolved), so
// every consumer below is name-based and says so where that matters.
export interface Occurrence {
  offset: number;
  length: number;
  name: string;
  /** The dotted receiver text immediately before this name, or null. */
  qualifier: string | null;
}

export const IDENT_START = /[A-Za-z_]/;
export const IDENT_PART = /[A-Za-z0-9_]/;

/** The index of the last non-whitespace character at or before `from`, or -1. */
function skipSpaceBack(text: string, from: number): number {
  let i = from;
  while (i >= 0 && (text[i] === " " || text[i] === "\t" || text[i] === "\n" || text[i] === "\r")) i--;
  return i;
}

export function scanIdentifiers(text: string): Occurrence[] {
  const out: Occurrence[] = [];
  let i = 0;
  const n = text.length;

  while (i < n) {
    const c = text[i];

    // Line comment: `//` in any of its three forms.
    if (c === "/" && text[i + 1] === "/") {
      while (i < n && text[i] !== "\n") i++;
      continue;
    }

    // Block comment, nesting. Depth, not "find the first */".
    if (c === "/" && text[i + 1] === "*") {
      let depth = 1;
      i += 2;
      while (i < n && depth > 0) {
        if (text[i] === "/" && text[i + 1] === "*") {
          depth++;
          i += 2;
        } else if (text[i] === "*" && text[i + 1] === "/") {
          depth--;
          i += 2;
        } else {
          i++;
        }
      }
      continue;
    }

    // String literal. Skipped, except that `${...}` inside it is code: the scan
    // recurses into the interpolation's text and shifts the offsets back into
    // this buffer's coordinates, so an occurrence inside an interpolation is
    // reported at its true position.
    if (c === '"') {
      i++;
      while (i < n && text[i] !== '"') {
        if (text[i] === "\\") {
          i += 2;
          continue;
        }
        if (text[i] === "$" && text[i + 1] === "{") {
          const exprStart = i + 2;
          // Track brace depth so a nested `${ m.get("${k}") }` — or any braced
          // subexpression — ends at the right `}`.
          let depth = 1;
          let j = exprStart;
          while (j < n && depth > 0) {
            if (text[j] === "{") depth++;
            else if (text[j] === "}") depth--;
            if (depth > 0) j++;
          }
          for (const inner of scanIdentifiers(text.slice(exprStart, j))) {
            out.push({ ...inner, offset: inner.offset + exprStart });
          }
          i = j + 1;
          continue;
        }
        i++;
      }
      i++;
      continue;
    }

    if (IDENT_START.test(c)) {
      const start = i;
      while (i < n && IDENT_PART.test(text[i])) i++;
      // The qualifier is the receiver text before a `.`, so a consumer can tell
      // `Iter.map` from a bare `map`. Whitespace is skipped on BOTH sides of the
      // dot, and newlines count as whitespace on both: a method chain is
      // routinely broken before the dot (`value\n  .trim()`) and occasionally
      // after it, and treating a newline as whitespace on one side only silently
      // drops the qualifier for one of the two layouts.
      let qualifier: string | null = null;
      let k = skipSpaceBack(text, start - 1);
      if (k >= 0 && text[k] === ".") {
        let q = skipSpaceBack(text, k - 1);
        const qEnd = q + 1;
        while (q >= 0 && IDENT_PART.test(text[q])) q--;
        if (q + 1 < qEnd) qualifier = text.slice(q + 1, qEnd);
      }
      out.push({ offset: start, length: i - start, name: text.slice(start, i), qualifier });
      continue;
    }

    i++;
  }

  return out;
}

/** Every code occurrence of `name` in `text`, comments and string bodies excluded. */
export function occurrencesOf(text: string, name: string): Occurrence[] {
  return scanIdentifiers(text).filter((o) => o.name === name);
}

/** Split a rendered parameter list on top-level commas only. */
export function splitParams(label: string): { text: string; start: number; end: number }[] {
  const open = label.indexOf("(");
  if (open === -1) return [];
  let depth = 0;
  let i = open;
  let segStart = open + 1;
  const parts: { text: string; start: number; end: number }[] = [];
  for (; i < label.length; i++) {
    const c = label[i];
    if (c === "(" || c === "<" || c === "[") depth++;
    else if (c === ")" || c === ">" || c === "]") {
      depth--;
      if (depth === 0) break;
    } else if (c === "," && depth === 1) {
      parts.push({ text: label.slice(segStart, i), start: segStart, end: i });
      segStart = i + 1;
    }
  }
  const tail = label.slice(segStart, i);
  if (tail.trim().length > 0) parts.push({ text: tail, start: segStart, end: i });
  return parts;
}

/**
 * The call being typed at `offset`: the callee name and which argument the
 * cursor is in. Walks backwards counting depth, so a nested `f(g(1, 2), |)`
 * reports `f` and argument 1, not `g`.
 */
export function callContextAt(text: string, offset: number): { name: string; argIndex: number } | null {
  let depth = 0;
  let argIndex = 0;
  for (let i = offset - 1; i >= 0; i--) {
    const c = text[i];
    if (c === ")" || c === "]" || c === "}") depth++;
    else if (c === "(") {
      if (depth === 0) {
        let e = i - 1;
        while (e >= 0 && /\s/.test(text[e])) e--;
        let s = e;
        while (s >= 0 && IDENT_PART.test(text[s])) s--;
        const name = text.slice(s + 1, e + 1);
        if (name.length === 0 || !IDENT_START.test(name[0])) return null;
        return { name, argIndex };
      }
      depth--;
    } else if (c === "[" || c === "{") {
      if (depth === 0) return null;
      depth--;
    } else if (c === "," && depth === 0) {
      argIndex++;
    } else if (c === ";" || c === "\n") {
      // A statement boundary at depth 0 means the cursor is not inside a call at
      // all. Without this the scan runs to the top of the file and reports the
      // first `(` it meets, offering help for an unrelated function.
      if (depth === 0 && c === ";") return null;
    }
  }
  return null;
}
