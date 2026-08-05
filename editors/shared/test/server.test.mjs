// End-to-end tests for the LSP server, driven over real stdio JSON-RPC against
// the real compiler.
//
// scan.test.mjs covers the pure helpers; nothing else did. Every handler in
// server.ts only runs when a client asks, so a handler that throws, returns the
// wrong shape, or silently returns nothing looks identical to one that works
// until an editor is attached by hand. These tests are that client.
//
// They need a `build/saffronc` that understands `--json`, so they SKIP rather
// than fail when the compiler is missing or too old — carrying the reason on
// each skipped test, since a suite that silently skips reads exactly like a
// green one.
//
// Run: node --test editors/shared/test/server.test.mjs
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { spawn, execFileSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const SHARED = path.resolve(HERE, "..");
const REPO = path.resolve(SHARED, "..", "..");
const SERVER = path.join(SHARED, "out", "server.js");
const COMPILER = path.join(REPO, "build", "saffronc");

/**
 * Why the suite cannot run, or `false` if it can.
 *
 * `false`, not `null`: node:test reads any non-`false`, non-`undefined` `skip`
 * value as a reason to skip, so returning `null` here skips the whole suite
 * while reporting it as 14 passing tests.
 */
function blocker() {
  if (!fs.existsSync(SERVER)) return `no built server at ${SERVER} — run editors/build.sh`;
  if (!fs.existsSync(COMPILER)) return `no compiler at ${COMPILER} — run ./bootstrap.sh`;
  // A compiler predating --json prints human-readable text and the server shows
  // nothing. Probe it rather than let every assertion fail with an empty payload.
  const probe = path.join(os.tmpdir(), `saffron-json-probe-${process.pid}.sf`);
  fs.writeFileSync(probe, "var x: Int = 1\n");
  try {
    const out = execFileSync(COMPILER, ["--json", probe, "/dev/null"], { encoding: "utf8" });
    JSON.parse(out);
  } catch {
    return `${COMPILER} does not support --json — rebuild it (./bootstrap.sh)`;
  } finally {
    fs.rmSync(probe, { force: true });
  }
  return false;
}

// Passed as each test's `skip`, so the reason is printed against every skipped
// test rather than the run just looking green.
const SKIP = blocker();

// One document exercised by every test. `radius` deliberately appears as a
// field, a parameter, an assignment target, a plain read and inside a `${}`
// interpolation, because those five are what distinguish the scanner from a
// regex.
const SRC = `/// Adds two numbers.
public fun add(a: Int, b: Int): Int {
    return a + b
}

/// A circle.
class Circle {
    var radius: Float
    private var label: String
    fun init(radius: Float) {
        this.radius = radius
        this.label = "r=\${radius}"
    }
    /// Area of the circle.
    fun area(): Float {
        return 3.14 * this.radius * this.radius
    }
}

actor Counter {
    var count: Int
    fun init() { this.count = 0 }
}

var total: Int = add(1, 2)
`;

let srv = null;
let probeFile = "";
let uri = "";
let caps = null;
let nextId = 1;
const pending = new Map();
const notifications = [];

function send(msg) {
  const s = JSON.stringify({ jsonrpc: "2.0", ...msg });
  srv.stdin.write(`Content-Length: ${Buffer.byteLength(s)}\r\n\r\n${s}`);
}

function request(method, params) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`${method}: timed out`)), 30000);
    pending.set(id, (m) => {
      clearTimeout(timer);
      m.error ? reject(new Error(`${method}: ${m.error.message}`)) : resolve(m.result);
    });
    send({ id, method, params });
  });
}

/** Position of the nth (0-based) occurrence of `needle` in SRC. */
const lines = SRC.split("\n");
function at(needle, occurrence = 0) {
  let seen = 0;
  for (let line = 0; line < lines.length; line++) {
    let idx = -1;
    while ((idx = lines[line].indexOf(needle, idx + 1)) !== -1) {
      if (seen++ === occurrence) return { line, character: idx };
    }
  }
  throw new Error(`test bug: ${JSON.stringify(needle)} not in the fixture`);
}

before(async () => {
  if (SKIP) return;

  // The probe lives outside the repo so a crashed run cannot leave a stray .sf
  // where the test suites would pick it up.
  probeFile = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "saffron-lsp-")), "probe.sf");
  fs.writeFileSync(probeFile, SRC);
  uri = "file://" + probeFile;

  srv = spawn(process.execPath, [SERVER, "--stdio"], { stdio: ["pipe", "pipe", "pipe"] });
  let buf = Buffer.alloc(0);
  srv.stdout.on("data", (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      const sep = buf.indexOf("\r\n\r\n");
      if (sep === -1) return;
      const len = /Content-Length: (\d+)/i.exec(buf.slice(0, sep).toString());
      if (!len) return;
      const total = sep + 4 + Number(len[1]);
      if (buf.length < total) return;
      const body = JSON.parse(buf.slice(sep + 4, total).toString());
      buf = buf.slice(total);
      if (body.id !== undefined && pending.has(body.id)) {
        pending.get(body.id)(body);
        pending.delete(body.id);
      } else if (body.method) {
        notifications.push(body);
      }
    }
  });

  const init = await request("initialize", {
    processId: process.pid,
    rootUri: "file://" + REPO,
    capabilities: {},
  });
  caps = init.capabilities;
  send({ method: "initialized", params: {} });
  send({
    method: "textDocument/didOpen",
    params: { textDocument: { uri, languageId: "saffron", version: 1, text: SRC } },
  });
  // onDidChangeContent is debounced 400ms and then shells out to the compiler.
  await new Promise((r) => setTimeout(r, 3000));
});

after(() => {
  if (srv) srv.kill();
  if (probeFile) fs.rmSync(path.dirname(probeFile), { recursive: true, force: true });
});

test("advertises every capability it implements", { skip: SKIP }, () => {
  // A handler registered without its capability flag is dead code: the client
  // never sends the request. This is the pairing that check catches.
  for (const flag of [
    "hoverProvider",
    "definitionProvider",
    "documentSymbolProvider",
    "workspaceSymbolProvider",
    "completionProvider",
    "referencesProvider",
    "documentHighlightProvider",
    "renameProvider",
    "signatureHelpProvider",
    "foldingRangeProvider",
    "semanticTokensProvider",
    "documentFormattingProvider",
  ]) {
    assert.ok(caps[flag], `missing capability: ${flag}`);
  }
  assert.ok(caps.renameProvider.prepareProvider, "rename must advertise prepareProvider");
});

test("document symbols nest members under their container", { skip: SKIP }, async () => {
  const syms = await request("textDocument/documentSymbol", { textDocument: { uri } });
  assert.deepEqual(
    syms.map((s) => s.name).sort(),
    ["Circle", "Counter", "add", "total"],
    "top level should hold exactly the four file-scope declarations",
  );
  const circle = syms.find((s) => s.name === "Circle");
  assert.deepEqual(
    circle.children.map((c) => c.name).sort(),
    ["area", "init", "label", "radius"],
    "fields and methods both belong to the class",
  );
  // Two classes each declare `init`; a flat list could not say which is which.
  const counter = syms.find((s) => s.name === "Counter");
  assert.ok(counter.children.some((c) => c.name === "init"));
  assert.ok(circle.range.end.line > circle.range.start.line, "extent should span the body");
  assert.equal(circle.selectionRange.start.line, at("class Circle").line);
});

test("hover shows the signature, the doc comment, and non-default visibility", { skip: SKIP }, async () => {
  const fn = await request("textDocument/hover", { textDocument: { uri }, position: at("add(a") });
  assert.match(fn.contents.value, /\(a: Int, b: Int\): Int/);
  assert.match(fn.contents.value, /Adds two numbers/);

  const priv = await request("textDocument/hover", { textDocument: { uri }, position: at("label") });
  assert.match(priv.contents.value, /private/, "a private field must say so");

  const pub = await request("textDocument/hover", { textDocument: { uri }, position: at("radius", 0) });
  assert.doesNotMatch(pub.contents.value, /public/, "the default visibility is noise, not signal");
});

test("go-to-definition resolves a this-qualified field and method to their declarations", { skip: SKIP }, async () => {
  // The receiver's static type is not in the payload, so a member is resolved by
  // name and — for `this.` — disambiguated by the enclosing class. Before this
  // worked, the definition handler only understood MODULE qualifiers, so every
  // `this.x`/`obj.x` resolved nothing: no jump, and IntelliJ correspondingly
  // refused to underline the identifier on Cmd-hover.
  const fieldDecl = at("var radius").line; // the declaration line of Circle.radius

  // `this.radius` inside area() is the 2nd `radius` after the field decl and its
  // two init uses; target the one in the multiplication.
  const useInArea = at("this.radius", 2); // 0: init assign, 1/2: area()'s two uses
  const toField = await request("textDocument/definition", {
    textDocument: { uri },
    position: { line: useInArea.line, character: useInArea.character + "this.".length + 1 },
  });
  assert.ok(toField, "this.radius must resolve");
  assert.equal(toField.range.start.line, fieldDecl, "…to the field declaration");

  // `this.count` inside Counter.init resolves to Counter.count, not to any other
  // `count` — the container disambiguation.
  const countDecl = at("var count").line;
  const countUse = at("this.count", 0);
  const toCount = await request("textDocument/definition", {
    textDocument: { uri },
    position: { line: countUse.line, character: countUse.character + "this.".length + 1 },
  });
  assert.ok(toCount, "this.count must resolve");
  assert.equal(toCount.range.start.line, countDecl, "…to the actor's own field");
});

test("references include interpolated uses and exclude comment text", { skip: SKIP }, async () => {
  const refs = await request("textDocument/references", {
    textDocument: { uri },
    position: at("radius", 0),
    context: { includeDeclaration: true },
  });
  const found = refs.map((r) => `${r.range.start.line}:${r.range.start.character}`);
  // decl, init param, this.radius, = radius, ${radius}, and two in area().
  assert.equal(refs.length, 7, `expected 7 occurrences of radius, got ${found.join(" ")}`);
  const interpLine = at('r=${radius}').line;
  assert.ok(found.some((f) => f.startsWith(`${interpLine}:`)), "the ${radius} use is a real reference");
});

test("document highlight marks the declaration as a write", { skip: SKIP }, async () => {
  const hl = await request("textDocument/documentHighlight", {
    textDocument: { uri },
    position: at("radius", 0),
  });
  assert.ok(hl.length >= 6);
  assert.ok(hl.some((h) => h.kind === 3), "DocumentHighlightKind.Write should mark the declaration");
});

test("folding ranges never collapse to a single line", { skip: SKIP }, async () => {
  const folds = await request("textDocument/foldingRange", { textDocument: { uri } });
  assert.ok(folds.length > 0);
  for (const f of folds) {
    // A range whose end equals its start renders as a fold control that does
    // nothing when clicked, which reads as a broken editor.
    assert.ok(f.endLine > f.startLine, `degenerate fold: ${JSON.stringify(f)}`);
  }
});

test("signature help resolves the innermost call and the active argument", { skip: SKIP }, async () => {
  const call = at("add(1, 2)");
  const first = await request("textDocument/signatureHelp", {
    textDocument: { uri },
    position: { line: call.line, character: call.character + 4 },
  });
  assert.match(first.signatures[0].label, /^add\(/);
  assert.equal(first.signatures[0].parameters.length, 2, "Int, Int — not split on a nested comma");
  assert.equal(first.activeParameter, 0);

  const second = await request("textDocument/signatureHelp", {
    textDocument: { uri },
    position: { line: call.line, character: call.character + 7 },
  });
  assert.equal(second.activeParameter, 1, "past the comma the second parameter is active");
});

test("semantic tokens are well-formed deltas within the legend", { skip: SKIP }, async () => {
  const { data } = await request("textDocument/semanticTokens/full", { textDocument: { uri } });
  assert.ok(data.length > 0);
  assert.equal(data.length % 5, 0, "tokens are 5-tuples");
  const typeCount = caps.semanticTokensProvider.legend.tokenTypes.length;
  const modCount = caps.semanticTokensProvider.legend.tokenModifiers.length;
  for (let i = 0; i < data.length; i += 5) {
    // Encoding is relative: a negative delta means the tokens were emitted out
    // of order, which silently shifts every colour after it.
    assert.ok(data[i] >= 0, `negative line delta at ${i}`);
    assert.ok(data[i + 1] >= 0, `negative char delta at ${i}`);
    assert.ok(data[i + 2] > 0, `zero-length token at ${i}`);
    assert.ok(data[i + 3] < typeCount, `token type out of legend at ${i}`);
    assert.ok(data[i + 4] < (1 << modCount), `modifier bit out of legend at ${i}`);
  }
});

test("rename rewrites code occurrences only, interpolations included", { skip: SKIP }, async () => {
  const prep = await request("textDocument/prepareRename", {
    textDocument: { uri },
    position: at("radius", 0),
  });
  assert.ok(prep && prep.range, "a name the compiler declared here is renameable");

  const edit = await request("textDocument/rename", {
    textDocument: { uri },
    position: at("radius", 0),
    newName: "r2",
  });
  const edits = edit.changes[uri];
  assert.equal(edits.length, 7);

  // Apply back-to-front so earlier edits do not shift later offsets.
  let applied = SRC;
  const ordered = [...edits].sort(
    (a, b) => b.range.start.line - a.range.start.line || b.range.start.character - a.range.start.character,
  );
  for (const e of ordered) {
    const ls = applied.split("\n");
    const l = ls[e.range.start.line];
    ls[e.range.start.line] = l.slice(0, e.range.start.character) + e.newText + l.slice(e.range.end.character);
    applied = ls.join("\n");
  }
  assert.match(applied, /r=\$\{r2\}/, "the interpolation is code and must be rewritten");
  assert.match(applied, /\/\/\/ Area of the circle\./, "comments are untouched");
  assert.doesNotMatch(applied, /\bradius\b/, "no occurrence left behind");
});

test("rename refuses names the compiler did not declare here", { skip: SKIP }, async () => {
  // `Float` is a type reference, not a local declaration. Renaming it would
  // rewrite this file and leave every other user of the name broken, so
  // prepareRename must decline instead of offering a partial edit.
  await assert.rejects(
    () => request("textDocument/prepareRename", { textDocument: { uri }, position: at("Float") }),
    /not declared/,
  );
});

test("workspace symbols filter by query and carry their container", { skip: SKIP }, async () => {
  const hits = await request("workspace/symbol", { query: "circ" });
  assert.ok(hits.some((s) => s.name === "Circle"), "matching is case-insensitive and partial");

  const all = await request("workspace/symbol", { query: "" });
  assert.ok(all.some((s) => s.containerName === "Circle"), "a member reports its owner");
});

test("completion offers scope-visible names with signatures, not members", { skip: SKIP }, async () => {
  const res = await request("textDocument/completion", { textDocument: { uri }, position: at("total") });
  const items = res.items ?? res;
  const add = items.find((i) => i.label === "add");
  assert.ok(add, "a top-level function is in scope");
  assert.match(add.detail, /Int/, "the signature is worth showing next to the name");
  // A field is reachable as `this.radius`, never as a bare `radius`; offering it
  // as a bare completion inserts something that does not compile.
  assert.ok(!items.some((i) => i.label === "radius"), "members are not bare-name completions");
});

test("a clean file publishes an empty diagnostic list", { skip: SKIP }, () => {
  // Empty, not absent: the client clears squiggles only when it receives a list.
  const last = notifications.filter((n) => n.method === "textDocument/publishDiagnostics").pop();
  assert.ok(last, "diagnostics must be published even when there are none");
  assert.deepEqual(last.params.diagnostics, []);
});

test("a type error is reported at its real line and column", { skip: SKIP }, async () => {
  // The point of the whole compiler-side change: before spans reached the
  // checker, a type error had no position and could not be shown at all.
  const bad = 'var x: Int = "nope"\n';
  const badUri = uri.replace("probe.sf", "bad.sf");
  fs.writeFileSync(fileURLToPath(badUri), bad);
  notifications.length = 0;
  send({
    method: "textDocument/didOpen",
    params: { textDocument: { uri: badUri, languageId: "saffron", version: 1, text: bad } },
  });
  await new Promise((r) => setTimeout(r, 3000));
  const note = notifications
    .filter((n) => n.method === "textDocument/publishDiagnostics" && n.params.uri === badUri)
    .pop();
  assert.ok(note, "the bad file got diagnostics");
  assert.equal(note.params.diagnostics.length, 1);
  const d = note.params.diagnostics[0];
  assert.equal(d.range.start.line, 0);
  assert.equal(d.range.start.character, 4, "the squiggle sits on `x`, not at column 0");
  assert.match(d.message, /Int/);
});

test("an EXPRESSION-level type error squiggles the expression", { skip: SKIP }, async () => {
  // The case above is about a *declaration* (`var x`), which has had a span
  // since WS1a. This one is about an expression, which did not: a wrong argument
  // type is the most common type error there is and it used to arrive with no
  // position at all, so the server had nothing to attach a range to and the
  // squiggle could not be drawn. Asserting it here rather than only on the
  // compiler side, because the compiler emitting an offset and the client
  // turning that offset into the right zero-based line/character are two
  // separate things that can each be wrong.
  const bad = 'fun take(s: String): Int { return 1 }\nvar n: Int = 3\nvar r: Int = take(n)\n';
  const badUri = uri.replace("probe.sf", "argerr.sf");
  fs.writeFileSync(fileURLToPath(badUri), bad);
  notifications.length = 0;
  send({
    method: "textDocument/didOpen",
    params: { textDocument: { uri: badUri, languageId: "saffron", version: 1, text: bad } },
  });
  await new Promise((r) => setTimeout(r, 3000));
  const note = notifications
    .filter((n) => n.method === "textDocument/publishDiagnostics" && n.params.uri === badUri)
    .pop();
  assert.ok(note, "the file got diagnostics");
  const d = note.params.diagnostics.find((x) => /argument 1/.test(x.message));
  assert.ok(d, `expected an argument-type error, got ${JSON.stringify(note.params.diagnostics)}`);

  // Zero-based here, 1-based in the compiler's JSON: line 3 -> 2, column 19 ->
  // 18. Getting this conversion wrong by one is the whole risk of the client
  // half, and it is invisible in a screenshot.
  assert.equal(d.range.start.line, 2, "on the line the call is on");
  assert.equal(d.range.start.character, 18, "and on `n`, the offending argument");

  // The range covers exactly the argument and nothing more. Sliced out of the
  // document text rather than compared as numbers, for the same reason the
  // compiler-side test slices: an end column that reads plausibly can still be
  // one character long in the wrong place.
  const line = bad.split("\n")[d.range.start.line];
  assert.equal(
    line.slice(d.range.start.character, d.range.end.character),
    "n",
    "the squiggle covers the argument alone, not the whole call",
  );
});

// The compiler reports BYTE columns and offsets — `AST.Span` documents it and the
// lexer counts `col` in bytes to match — while LSP `Position.character` is UTF-16
// code units. Every byte a character contributes beyond its UTF-16 width pushes a
// range right. Nothing else in this suite could see it, because every other
// fixture here is pure ASCII, where the two units coincide exactly.
//
// The server does that conversion in two independent places and each can be right
// while the other is wrong, so there is a test for each:
//
//   per-line byte COLUMN  — diagnostics, symbol name ranges, go-to-definition.
//     Sensitive to multi-byte characters to the LEFT on the same line.
//   whole-file byte OFFSET — documentSymbol extents and folding ranges, via
//     start_offset/end_offset. Sensitive to multi-byte characters ANYWHERE
//     ABOVE, since the count accumulates over the whole file.
//
// A fixture with non-ASCII on an earlier line does NOT test the column path: the
// column is measured from the start of its own line, so it is unaffected. That
// distinction is worth stating because the obvious single test covers only one of
// the two conversions and passes regardless of the other.
test("a non-ASCII line above does not shift a symbol extent", { skip: SKIP }, async () => {
  // Three `★` (3 bytes, 1 unit each) above the class put the compiler's byte
  // offsets 6 ahead of the string indices `positionAt` expects, so the outline
  // extent and the fold both slide down/right by 6 units' worth of text.
  const src = 'var label: String = "★★★"\nclass Widget {\n    var w: Int\n    fun init() { this.w = 1 }\n}\n';
  const symUri = uri.replace("probe.sf", "utf8sym.sf");
  fs.writeFileSync(fileURLToPath(symUri), src);
  send({
    method: "textDocument/didOpen",
    params: { textDocument: { uri: symUri, languageId: "saffron", version: 1, text: src } },
  });
  await new Promise((r) => setTimeout(r, 3000));
  const syms = await request("textDocument/documentSymbol", { textDocument: { uri: symUri } });
  const widget = syms.find((s) => s.name === "Widget");
  assert.ok(widget, `expected a Widget symbol, got ${syms.map((s) => s.name).join(",")}`);

  // Asserted against the text rather than as numbers, for the same reason as
  // everywhere else here: an extent off by six reads as a plausible range.
  const srcLines = src.split("\n");
  assert.equal(
    srcLines[widget.selectionRange.start.line].slice(
      widget.selectionRange.start.character,
      widget.selectionRange.end.character,
    ),
    "Widget",
    "the name range lands on the class name",
  );
  // The extent must start at `class` — one unit-count error and it starts mid-token
  // or on the line above.
  assert.equal(widget.range.start.line, 1, "the extent begins on the class's own line");
  assert.equal(widget.range.start.character, 0, "at column 0, where `class` starts");
  assert.equal(widget.range.end.line, 4, "and ends on the closing brace's line");
});

test("a multi-byte character BEFORE the error on its own line", { skip: SKIP }, async () => {
  // The column path: three 2-byte `é` to the left of the offending argument. If
  // the byte column reached the client unconverted the squiggle would sit three
  // characters right of `n`.
  const bad = 'fun take(s: String): Int { return 1 }\nvar n: Int = 3\nvar r: Int = take(/* é é é */ n)\n';
  const badUri = uri.replace("probe.sf", "utf8col.sf");
  fs.writeFileSync(fileURLToPath(badUri), bad);
  notifications.length = 0;
  send({
    method: "textDocument/didOpen",
    params: { textDocument: { uri: badUri, languageId: "saffron", version: 1, text: bad } },
  });
  await new Promise((r) => setTimeout(r, 3000));
  const note = notifications
    .filter((n) => n.method === "textDocument/publishDiagnostics" && n.params.uri === badUri)
    .pop();
  assert.ok(note, "the file got diagnostics");
  const d = note.params.diagnostics.find((x) => /argument 1/.test(x.message));
  assert.ok(d, `expected an argument-type error, got ${JSON.stringify(note.params.diagnostics)}`);
  const line = bad.split("\n")[d.range.start.line];
  assert.equal(
    line.slice(d.range.start.character, d.range.end.character),
    "n",
    "three 2-byte characters to the left must not shift the range by three",
  );
});
