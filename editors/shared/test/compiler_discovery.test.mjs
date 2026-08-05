// Does the server find the compiler when the workspace root is NOT the repo?
//
// server.test.mjs cannot answer this. It initializes with `rootUri` pointing at
// the repo, and `onInitialize` independently probes `<rootUri>/build/saffronc`
// and overwrites whatever findCompiler() returned. So every existing test passes
// with findCompiler() completely broken — which it was, from the commit that
// introduced it (a9bac93) until 2026-08-04: it resolved two levels up from
// `editors/shared/out`, landing on `editors/`, and probed
// `editors/build/saffronc`, a path that never exists.
//
// The uncovered case is the one a user hits first: a sample project in some other
// directory, or a single file opened with no rootUri at all. Then findCompiler()
// is the only answer, and it fell through to the bare name `saffronc` — so
// everything depended on that being on PATH, and when it wasn't the editor showed
// no diagnostics rather than an error.
//
// Run: node --test editors/shared/test/compiler_discovery.test.mjs
import { test } from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const SHARED = path.resolve(HERE, "..");
const REPO = path.resolve(SHARED, "..", "..");
const SERVER = path.join(SHARED, "out", "server.js");
const COMPILER = path.join(REPO, "build", "saffronc");

const SKIP = !fs.existsSync(SERVER)
  ? `no built server at ${SERVER} — run editors/build.sh`
  : !fs.existsSync(COMPILER)
    ? `no compiler at ${COMPILER} — run ./bootstrap.sh`
    : false;

/**
 * Start a server, initialize it with the given rootUri (or none), open a file
 * with a type error, and return { diagnostics, log }.
 *
 * `env` lets a caller strip PATH, which is what makes the bare-name fallback
 * distinguishable from a real find: with no PATH, `saffronc` cannot spawn, so a
 * server that reports diagnostics must have located the binary by path.
 */
async function probe({ rootUri, cwd, env, initializationOptions, expectDiagnostics }) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "saffron-disco-"));
  const file = path.join(dir, "bad.sf");
  // `cannot assign String to Int` is one of the 11 checker errors that carries a
  // span (`error_at`), so it arrives with located:true and lands on its own
  // range. Chosen deliberately: an argument-type mismatch would also prove the
  // compiler ran, but it is span-less, so it would test the unlocated path here
  // while these tests are about discovery. The unlocated path has its own test.
  const src = 'var z: Int = "hi"\n';
  fs.writeFileSync(file, src);
  const result = await probeFile({
    file, src, rootUri, cwd: cwd ?? dir, env, initializationOptions, expectDiagnostics,
  });
  fs.rmSync(dir, { recursive: true, force: true });
  return result;
}

/**
 * The same round trip over a caller-supplied file and source, for the cases that
 * need to choose the program rather than the discovery conditions.
 */
async function probeFile({ file, src, rootUri, cwd, env, initializationOptions, expectDiagnostics }) {
  const uri = "file://" + file;

  const srv = spawn(process.execPath, [SERVER, "--stdio"], {
    stdio: ["pipe", "pipe", "pipe"],
    cwd,
    env: env ?? process.env,
  });

  const notifications = [];
  const pending = new Map();
  let nextId = 1;
  let stderr = "";
  srv.stderr.on("data", (c) => {
    stderr += c.toString();
  });

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

  const send = (msg) => {
    const s = JSON.stringify({ jsonrpc: "2.0", ...msg });
    srv.stdin.write(`Content-Length: ${Buffer.byteLength(s)}\r\n\r\n${s}`);
  };
  const request = (method, params) =>
    new Promise((resolve) => {
      const id = nextId++;
      pending.set(id, (body) => resolve(body.result));
      send({ id, method, params });
    });

  await request("initialize", {
    processId: process.pid,
    rootUri,
    capabilities: {},
    initializationOptions,
  });
  send({ method: "initialized", params: {} });
  send({
    method: "textDocument/didOpen",
    params: { textDocument: { uri, languageId: "saffron", version: 1, text: src } },
  });
  // Wait for the diagnostics notification rather than for a fixed duration.
  // The server debounces 400ms and then shells out to the compiler, so the total
  // is machine- and load-dependent: a flat `setTimeout(3000)` passed on an idle
  // box and failed under a concurrent bootstrap, reporting `null` — which reads
  // exactly like the server having produced nothing. Polling keeps the fast case
  // fast while still bounding the slow one.
  //
  // `expectDiagnostics: false` is for the callers that assert on *silence* (an
  // unrunnable compiler): they must not wait out the whole ceiling for a
  // notification that is never coming.
  const deadline = Date.now() + (expectDiagnostics === false ? 3000 : 30000);
  const seen = () =>
    notifications.some(
      (n) => n.method === "textDocument/publishDiagnostics" && n.params.uri === uri,
    );
  while (!seen() && Date.now() < deadline) {
    await new Promise((r) => setTimeout(r, 50));
  }

  srv.kill();

  const note = notifications
    .filter((n) => n.method === "textDocument/publishDiagnostics" && n.params.uri === uri)
    .pop();
  // The server logs its chosen compiler via window/logMessage.
  const logged = notifications
    .filter((n) => n.method === "window/logMessage" || n.method === "window/showMessage")
    .map((n) => n.params.message)
    .join("\n");
  return { diagnostics: note ? note.params.diagnostics : null, log: logged, stderr };
}

test("finds the compiler with NO rootUri at all", { skip: SKIP }, async () => {
  // No rootUri means onInitialize's fallback cannot fire, so findCompiler() is
  // the only thing that can locate the binary. PATH is stripped so a bare
  // `saffronc` cannot spawn: a diagnostic here proves a real path was resolved.
  const env = { ...process.env, PATH: "/nonexistent" };
  const { diagnostics, log } = await probe({ rootUri: null, cwd: os.tmpdir(), env });

  assert.match(
    log,
    /using compiler: .*build\/saffronc/,
    `expected an absolute build/saffronc path, got log:\n${log}`,
  );
  assert.ok(diagnostics, "the file got a publishDiagnostics notification");
  assert.ok(
    diagnostics.some((d) => /cannot assign String to Int/.test(d.message)),
    `expected the assignment type error, got ${JSON.stringify(diagnostics)}`,
  );
});

test("finds the compiler when rootUri is an unrelated directory", { skip: SKIP }, async () => {
  // The realistic case: a sample Saffron project that is not the compiler repo.
  // onInitialize probes <root>/build/saffronc, finds nothing, and leaves
  // findCompiler()'s answer standing.
  const elsewhere = fs.mkdtempSync(path.join(os.tmpdir(), "saffron-proj-"));
  const env = { ...process.env, PATH: "/nonexistent" };
  const { diagnostics, log } = await probe({
    rootUri: "file://" + elsewhere,
    cwd: elsewhere,
    env,
  });
  fs.rmSync(elsewhere, { recursive: true, force: true });

  assert.ok(
    !log.includes("using compiler: saffronc"),
    `fell through to the bare PATH name; log:\n${log}`,
  );
  assert.ok(diagnostics, "the file got a publishDiagnostics notification");
  assert.ok(
    diagnostics.some((d) => /cannot assign String to Int/.test(d.message)),
    `expected the assignment type error, got ${JSON.stringify(diagnostics)}`,
  );
});

test("an unrunnable compiler is reported, not silently treated as clean", { skip: SKIP }, async () => {
  // The second half of the same defect. execFile's ENOENT/EACCES lands in the
  // same catch as "the compiler exited non-zero because the file has errors",
  // and an empty stdout parses to zero diagnostics — so a compiler that cannot
  // run was reported to the editor as a file with no problems. That is the most
  // misleading possible answer: worse than an error, because it looks like
  // success.
  // Point the server at a path that cannot be executed, via the explicit
  // `compilerPath` setting — which takes priority over every discovery step.
  // That is the only reliable way to force the failure now that findCompiler()
  // walks up and would otherwise locate the repo's real, working compiler.
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "saffron-nocomp-"));
  const fake = path.join(dir, "saffronc");
  fs.writeFileSync(fake, "#!/bin/sh\nexit 0\n");
  fs.chmodSync(fake, 0o000); // present but not executable -> EACCES

  const { diagnostics, log, stderr } = await probe({
    rootUri: "file://" + dir,
    cwd: dir,
    initializationOptions: { compilerPath: fake },
    expectDiagnostics: false,
  });
  fs.chmodSync(fake, 0o700);
  fs.rmSync(dir, { recursive: true, force: true });

  // The defect being pinned is the *silence*, so assert on it directly: a file
  // with a real type error must not come back as an empty diagnostics list.
  assert.ok(
    !diagnostics || diagnostics.length === 0,
    "sanity: an unrunnable compiler cannot produce real diagnostics",
  );

  const all = log + stderr;
  assert.match(
    all,
    /cannot run the compiler/i,
    `expected an explicit cannot-run report, got:\n${all}`,
  );
});

// A source with no span attached still has to reach the editor.
//
// Most checker errors carry no source region: 20 of 31 diagnostic sites in
// checker.sf still call the span-less `error`/`warn`. mapDiagnostics used to
// `filter` those out on the argument that 0:0 "would blame the top of the file",
// and a comment claimed they surfaced through a message pane — which nothing
// implemented. So the single most ordinary type error in the language, passing
// an Int where a String is declared, showed nothing whatsoever in the editor.
test("an unlocated diagnostic is still reported, not dropped", { skip: SKIP }, async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "saffron-unloc-"));
  const file = path.join(dir, "unloc.sf");
  // Verified span-less against `saffronc --json` directly: this emits
  // located:false, which is the whole point of the case.
  const src = 'fun take(s: String): Int { return 1 }\nvar r: Int = take(42)\n';
  fs.writeFileSync(file, src);

  const { diagnostics } = await probeFile({ file, src, rootUri: "file://" + REPO, cwd: dir });
  fs.rmSync(dir, { recursive: true, force: true });

  assert.ok(diagnostics, "the file got a publishDiagnostics notification");
  const hit = diagnostics.find((d) => /expects String, got Int/.test(d.message));
  assert.ok(hit, `the unlocated type error reached the editor; got ${JSON.stringify(diagnostics)}`);
  // And it must not silently pose as a precise location.
  assert.match(
    hit.message,
    /no source location/,
    "an unlocated diagnostic says so, so line 1 is not read as a claim",
  );
});

// An incomplete buffer is the normal state of a file being typed, and it used to
// KILL the compiler outright: `parse_error` reports without consuming, so at EOF
// the parser kept calling advance() and `this.tokens[this.pos]` raised
// `IndexError: index N out of bounds`. That is a fatal runtime error, not a
// diagnostic — it exits before writing any JSON. Every one of `var x =`,
// `fun f(`, `if (`, `1 +` reproduced it. Invisible to the suite, whose inputs are
// all complete files; unavoidable in an editor.
//
// The guard against over-correcting: clamping the cursor to EOF must not turn the
// crash into a HANG, which is strictly worse in an editor. Each case below has to
// come back within the timeout, so a spin fails this test rather than passing it.
test("incomplete buffers produce diagnostics, not a crash or a hang", { skip: SKIP }, async () => {
  const cases = ["var x: Int = ", "var x = ", "fun f(", "if (", "var a = 1 +", "class C {"];
  for (const src of cases) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "saffron-partial-"));
    const file = path.join(dir, "partial.sf");
    fs.writeFileSync(file, src + "\n");
    const { diagnostics } = await probeFile({
      file,
      src: src + "\n",
      rootUri: "file://" + REPO,
      cwd: dir,
    });
    fs.rmSync(dir, { recursive: true, force: true });

    assert.ok(
      diagnostics && diagnostics.length > 0,
      `${JSON.stringify(src)} must yield a parse diagnostic, got ${JSON.stringify(diagnostics)}`,
    );
  }
});
