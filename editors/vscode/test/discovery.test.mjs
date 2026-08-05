// Where does the extension look for the LSP server?
//
// This is the one thing about the VS Code client that fails *silently*. A
// LanguageClient constructed against a server module that does not exist starts,
// reports nothing, and leaves every .sf file looking clean — the same appearance
// as working correctly on code with no problems. There is no error dialog, no
// entry in the Problems panel, and the output channel is empty because the client
// never got far enough to open one.
//
// It was broken exactly that way: `asAbsolutePath("../shared/out/server.js")`
// resolves against the extension's own directory, so in an *installed* extension
// the `..` escapes into ~/.vscode/extensions and points at nothing. Only the F5
// "run from source" path worked, which is the one path a developer uses and a user
// never does — so the .vsix route the README documents shipped a dead extension.
//
// The test loads the COMPILED out/extension.js (not the TypeScript) with `vscode`
// and the language client stubbed, and asserts which path activate() hands to the
// client. Compiled, because a type-level check cannot see a wrong runtime path.

import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import Module from "node:module";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const VSCODE_DIR = path.resolve(HERE, "..");
const BUILT = path.join(VSCODE_DIR, "out", "extension.js");

/**
 * Activate the compiled extension against a synthetic environment and report the
 * server path it chose.
 *
 * `extDir` is what the extension believes its own directory is — the lever the
 * whole bug turned on. Point it at a scratch directory and you have simulated an
 * installed extension; point it at editors/vscode and you have simulated F5.
 */
function activateWith({ extDir, workspaceFolders = undefined, config = {} }) {
  let started = null;
  let errorShown = null;

  const stubs = {
    vscode: {
      window: {
        showErrorMessage: (m) => {
          errorShown = m;
        },
      },
      workspace: {
        getConfiguration: () => ({ get: (k) => config[k] ?? "" }),
        workspaceFolders: workspaceFolders?.map((p) => ({ uri: { fsPath: p } })),
      },
    },
    "vscode-languageclient/node": {
      LanguageClient: class {
        constructor(_id, _name, serverOptions, clientOptions) {
          started = { module: serverOptions.run.module, clientOptions };
        }
        start() {}
        stop() {
          return Promise.resolve();
        }
      },
      TransportKind: { ipc: 1 },
    },
  };

  // Intercept both resolution and loading: `vscode` has no on-disk copy outside a
  // running IDE, so resolution has to be short-circuited before the load hook.
  const origResolve = Module._resolveFilename;
  const origLoad = Module._load;
  Module._resolveFilename = function (req, ...rest) {
    return req in stubs ? req : origResolve.call(this, req, ...rest);
  };
  Module._load = function (req, ...rest) {
    return req in stubs ? stubs[req] : origLoad.call(this, req, ...rest);
  };

  try {
    const require = createRequire(import.meta.url);
    // Fresh each time: activate() runs at require time in the module's own scope,
    // and a cached copy would report the first call's decision for every case.
    delete require.cache[require.resolve(BUILT)];
    const ext = require(BUILT);
    ext.activate({ asAbsolutePath: (p) => path.join(extDir, p) });
  } finally {
    Module._resolveFilename = origResolve;
    Module._load = origLoad;
  }

  return { started, errorShown };
}

// The compiled output is gitignored, so a checkout without a build has nothing to
// test. Skip loudly rather than fail — and never skip when it IS built, which is
// the mistake the shared suite made once (node:test treats any non-false skip
// value as a skip, so returning null for "nothing wrong" skipped everything).
const skip = fs.existsSync(BUILT)
  ? false
  : "editors/vscode/out/extension.js is not built — run editors/build.sh";

test("an installed extension finds the globally installed server", { skip }, () => {
  const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "saffron-vsix-"));
  const globalServer = path.join(os.homedir(), ".saffron", "lsp", "server.js");

  const { started, errorShown } = activateWith({ extDir: scratch });

  if (fs.existsSync(globalServer)) {
    assert.equal(started?.module, globalServer);
    assert.equal(errorShown, null);
  } else {
    // No global install on this machine: then the correct behaviour is to SAY so,
    // not to start a client against a path that is not there. That is the actual
    // regression being guarded — the old code started one unconditionally.
    assert.equal(started, null);
    assert.match(String(errorShown), /no language server found/);
  }
});

test("running from the repo prefers the sibling shared build", { skip }, () => {
  const sibling = path.join(VSCODE_DIR, "..", "shared", "out", "server.js");
  if (!fs.existsSync(sibling)) {
    // Cannot assert a preference for a file that is absent; the first test still
    // covers the fallback. (build.sh always produces this, so this is defensive.)
    return;
  }
  const { started } = activateWith({ extDir: VSCODE_DIR });
  assert.equal(started?.module, path.normalize(sibling));
});

test("an explicit saffron.serverPath wins over everything", { skip }, () => {
  const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "saffron-vsix-"));
  const fake = path.join(scratch, "my-server.js");
  fs.writeFileSync(fake, "");

  const { started } = activateWith({
    extDir: VSCODE_DIR, // the sibling build exists and must still lose
    config: { serverPath: fake },
  });
  assert.equal(started?.module, fake);
});

test("no compilerPath is sent when the user has not set one", { skip }, () => {
  // Sending one overrides the server's own discovery (project build/saffronc,
  // then ~/.saffron/bin, then PATH). The old client always sent `"saffron"` —
  // which is not even the binary's name, it is `saffronc` — so every spawn failed
  // with ENOENT and no file ever received diagnostics.
  const { started } = activateWith({ extDir: VSCODE_DIR });
  if (!started) return; // covered by the first test's fallback branch
  assert.deepEqual(started.clientOptions.initializationOptions, {});
});

test("a set compilerPath is forwarded verbatim", { skip }, () => {
  const { started } = activateWith({
    extDir: VSCODE_DIR,
    config: { compilerPath: "/somewhere/saffronc" },
  });
  if (!started) return;
  assert.deepEqual(started.clientOptions.initializationOptions, {
    compilerPath: "/somewhere/saffronc",
  });
});
