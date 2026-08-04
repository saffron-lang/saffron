#!/usr/bin/env node
// Regenerates the `builtins` data array in editors/shared/src/builtins.ts from
// the real stdlib sources in src/lib/*.sf.
//
//     node editors/shared/tools/gen-builtins.mjs          # rewrite in place
//     node editors/shared/tools/gen-builtins.mjs --check   # fail if stale
//
// Only the region between `// @generated-start` and `// @generated-end` is
// touched; the interfaces and exported helpers around it are hand-maintained.
//
// A module's public surface = its top-level (column 0) `fun` declarations that
// are neither `private` nor `_`-prefixed. Decorators (`@extern(...)`,
// `@intrinsic`) are stripped before the declaration is read. Methods inside a
// `class` are indented, so they are naturally skipped — this models functions
// only, matching what the LSP consumes.
//
// Modules with no .sf source (currently only `Task`, which is native) keep
// whatever hand-written entry already exists in the block: the generator parses
// the previous block and carries such entries through untouched.

import * as fs from "fs";
import * as path from "path";
import { fileURLToPath } from "url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, "..", "..", "..");
const TARGET = path.join(HERE, "..", "src", "builtins.ts");

const START = "// @generated-start (gen-builtins.mjs) — do not edit by hand";
const END = "// @generated-end";

// module name -> stdlib source, relative to the repo root. Modules absent from
// this map (Task) are native and preserved from the existing block.
const MODULE_SOURCES = {
  IO: "src/lib/io.sf",
  Task: null,
  Async: "src/lib/async.sf",
  Json: "src/lib/json.sf",
  Reflect: "src/lib/reflect.sf",
  Time: "src/lib/time.sf",
  Math: "src/lib/math.sf",
};

// ---------------------------------------------------------------- .sf parsing

/// Scan a balanced bracket pair starting at `src[open]`, honouring string
/// literals (so `@extern("i32 puts(void*)")` does not end early). Returns the
/// index just past the closing bracket, or -1 if unbalanced.
function scanBalanced(src, open, openCh, closeCh) {
  let depth = 0;
  let i = open;
  let quote = null;
  while (i < src.length) {
    const c = src[i];
    if (quote) {
      if (c === "\\") i++;
      else if (c === quote) quote = null;
    } else if (c === '"' || c === "'") {
      quote = c;
    } else if (c === openCh) {
      depth++;
    } else if (c === closeCh) {
      depth--;
      if (depth === 0) return i + 1;
    }
    i++;
  }
  return -1;
}

/// Strip leading `@decorator` / `@decorator(...)` tokens. Returns the rest of
/// the line, or null if the line is malformed.
function stripDecorators(line) {
  let rest = line;
  for (;;) {
    const m = /^@[A-Za-z_][A-Za-z0-9_]*/.exec(rest);
    if (!m) return rest;
    let after = m[0].length;
    if (rest[after] === "(") {
      const close = scanBalanced(rest, after, "(", ")");
      if (close === -1) return null;
      after = close;
    }
    rest = rest.slice(after).replace(/^\s+/, "");
  }
}

/// Parse one top-level declaration line into { name, params, returnType }, or
/// null if it is not a public `fun`.
function parseFunLine(line) {
  const rest = stripDecorators(line);
  if (rest === null) return null;
  if (/^private\b/.test(rest)) return null;

  const head = /^fun\s+([A-Za-z_][A-Za-z0-9_]*)\s*/.exec(rest);
  if (!head) return null;
  const name = head[1];
  if (name.startsWith("_")) return null;

  let i = head[0].length;
  if (rest[i] === "<") {
    // generic parameter list — recorded nowhere, just skipped
    const close = scanBalanced(rest, i, "<", ">");
    if (close === -1) return null;
    i = close;
  }
  if (rest[i] !== "(") return null;
  const parenEnd = scanBalanced(rest, i, "(", ")");
  if (parenEnd === -1) return null;
  const params = rest.slice(i + 1, parenEnd - 1).trim();

  let tail = rest.slice(parenEnd).trim();
  let returnType = "Nil"; // Saffron's implicit return type
  if (tail.startsWith(":")) {
    tail = tail.slice(1);
    const brace = tail.indexOf("{");
    const typeText = (brace === -1 ? tail : tail.slice(0, brace)).trim();
    if (typeText) returnType = typeText;
  }
  return { name, params, returnType };
}

function extractModuleFunctions(sourcePath) {
  const text = fs.readFileSync(sourcePath, "utf8");
  const fns = [];
  const seen = new Set();
  let pendingDecorators = "";
  for (const raw of text.split("\n")) {
    if (/^\s/.test(raw) || raw.trim() === "") {
      pendingDecorators = "";
      continue;
    }
    const line = (pendingDecorators + raw).trim();
    pendingDecorators = "";
    // A line that is nothing but decorators applies to the next line.
    const afterDecorators = stripDecorators(line);
    if (afterDecorators !== null && afterDecorators === "" && line.startsWith("@")) {
      pendingDecorators = line + " ";
      continue;
    }
    const fn = parseFunLine(line);
    if (fn && !seen.has(fn.name)) {
      seen.add(fn.name);
      fns.push(fn);
    }
  }
  return fns;
}

// ------------------------------------------------- existing block round-trip

const JSTR = '"(?:[^"\\\\]|\\\\.)*"';
const MODULE_NAME_RE = new RegExp(`^name:\\s*(${JSTR}),$`);
const FN_RE = new RegExp(
  `^\\{\\s*name:\\s*(${JSTR}),\\s*params:\\s*(${JSTR}),\\s*returnType:\\s*(${JSTR})\\s*\\},?$`
);

/// Read the previous generated block back into [{ name, functions }]. Used both
/// for the before/after report and to carry native (source-less) modules over.
function parseExistingBlock(block) {
  const modules = [];
  let current = null;
  for (const raw of block.split("\n")) {
    const line = raw.trim();
    const modMatch = MODULE_NAME_RE.exec(line);
    if (modMatch) {
      current = { name: JSON.parse(modMatch[1]), functions: [] };
      modules.push(current);
      continue;
    }
    const fnMatch = FN_RE.exec(line);
    if (fnMatch && current) {
      current.functions.push({
        name: JSON.parse(fnMatch[1]),
        params: JSON.parse(fnMatch[2]),
        returnType: JSON.parse(fnMatch[3]),
      });
    }
  }
  return modules;
}

function renderBlock(modules) {
  const out = [START, "const builtins: BuiltinModule[] = ["];
  for (const mod of modules) {
    out.push("  {");
    out.push(`    name: ${JSON.stringify(mod.name)},`);
    if (mod.native) {
      out.push("    // Native: no src/lib/*.sf source. Hand-maintained; the");
      out.push("    // generator copies this entry through unchanged.");
    }
    out.push("    functions: [");
    for (const fn of mod.functions) {
      out.push(
        `      { name: ${JSON.stringify(fn.name)}, params: ${JSON.stringify(
          fn.params
        )}, returnType: ${JSON.stringify(fn.returnType)} },`
      );
    }
    out.push("    ],");
    out.push("  },");
  }
  out.push("];");
  out.push(END);
  return out.join("\n");
}

// -------------------------------------------------------------------- driver

const original = fs.readFileSync(TARGET, "utf8");
const startIdx = original.indexOf(START);
const endIdx = original.indexOf(END);
if (startIdx === -1 || endIdx === -1) {
  console.error(`error: ${TARGET} is missing the @generated-start/@generated-end markers`);
  process.exit(1);
}
const oldBlock = original.slice(startIdx, endIdx + END.length);
const previous = parseExistingBlock(oldBlock);
const previousByName = new Map(previous.map((m) => [m.name, m]));

// Preserve the existing module order, then append any mapped module that the
// previous block did not have.
const names = [...previous.map((m) => m.name)];
for (const name of Object.keys(MODULE_SOURCES)) {
  if (!names.includes(name)) names.push(name);
}

const modules = [];
const report = [];
for (const name of names) {
  const rel = MODULE_SOURCES[name];
  const before = previousByName.get(name)?.functions.length ?? 0;
  if (rel) {
    const abs = path.join(REPO_ROOT, rel);
    if (!fs.existsSync(abs)) {
      console.error(`error: ${name} is mapped to missing source ${rel}`);
      process.exit(1);
    }
    const functions = extractModuleFunctions(abs);
    modules.push({ name, functions });
    report.push({ name, before, after: functions.length, source: rel });
  } else {
    const kept = previousByName.get(name);
    if (!kept) {
      console.error(`error: ${name} has no source and no existing hand-written entry`);
      process.exit(1);
    }
    modules.push({ name, functions: kept.functions, native: true });
    report.push({ name, before, after: kept.functions.length, source: "(native, hand-written)" });
  }
}

const newBlock = renderBlock(modules);
const updated = original.slice(0, startIdx) + newBlock + original.slice(endIdx + END.length);

for (const r of report) {
  const delta = r.after === r.before ? "=" : r.after > r.before ? `+${r.after - r.before}` : `${r.after - r.before}`;
  console.log(`${r.name.padEnd(8)} ${String(r.before).padStart(3)} -> ${String(r.after).padStart(3)}  (${delta})  ${r.source}`);
}

if (process.argv.includes("--check")) {
  if (updated !== original) {
    console.error("error: builtins.ts is stale — run node editors/shared/tools/gen-builtins.mjs");
    process.exit(1);
  }
  console.log("builtins.ts is up to date");
} else if (updated === original) {
  console.log("builtins.ts unchanged");
} else {
  fs.writeFileSync(TARGET, updated);
  console.log(`wrote ${path.relative(REPO_ROOT, TARGET)}`);
}
