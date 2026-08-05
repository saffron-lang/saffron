import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import { window, workspace, ExtensionContext } from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

/**
 * Locate the LSP server, in the same order the IntelliJ plugin's `findServerPath`
 * uses — deliberately, so a setup that works in one editor works in the other.
 *
 * This was previously the single expression
 * `context.asAbsolutePath(path.join("..", "shared", "out", "server.js"))`, which
 * only resolves when the extension is *run from the repo* (F5). `asAbsolutePath`
 * is relative to the extension's own directory, so in an installed extension the
 * `..` escapes into `~/.vscode/extensions/` and the path does not exist. The
 * `.vsix` route the README documents therefore produced an extension that
 * activated and then did nothing — silently, since a language client whose server
 * module is missing is not a failure the user is shown.
 */
function findServer(context: ExtensionContext): string | undefined {
  const configured = workspace.getConfiguration("saffron").get<string>("serverPath");
  const candidates = [
    ...(configured ? [configured] : []),
    // Running from the repo (F5): the sibling shared package.
    context.asAbsolutePath(path.join("..", "shared", "out", "server.js")),
    // The Saffron repo open as a workspace folder.
    ...(workspace.workspaceFolders ?? []).map((f) =>
      path.join(f.uri.fsPath, "editors", "shared", "out", "server.js"),
    ),
    // Installed by `editors/build.sh --install`.
    path.join(os.homedir(), ".saffron", "lsp", "server.js"),
  ];
  return candidates.find((p) => fs.existsSync(p));
}

export function activate(context: ExtensionContext) {
  const serverModule = findServer(context);
  if (!serverModule) {
    // Report it rather than starting a client against a path that is not there.
    // The alternative failure mode is a file showing no problems, which is
    // indistinguishable from clean code.
    void window.showErrorMessage(
      "Saffron: no language server found. Run `editors/build.sh --install` in the " +
        "Saffron repo, or set `saffron.serverPath`.",
    );
    return;
  }

  // Forward a compiler path only when the user actually set one. This used to
  // default to `"saffron"` and send it unconditionally, which overrode the
  // server's own discovery (project `build/saffronc`, then `~/.saffron/bin`, then
  // PATH) with a name that is not even the binary's — it is `saffronc`. So every
  // spawn failed with ENOENT and no file ever got diagnostics.
  const compilerPath =
    workspace.getConfiguration("saffron").get<string>("compilerPath") || undefined;

  const serverOptions: ServerOptions = {
    run: { module: serverModule, transport: TransportKind.ipc },
    debug: { module: serverModule, transport: TransportKind.ipc },
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "saffron" }],
    initializationOptions: compilerPath ? { compilerPath } : {},
  };

  client = new LanguageClient(
    "saffron",
    "Saffron Language Server",
    serverOptions,
    clientOptions,
  );

  client.start();
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) return undefined;
  return client.stop();
}
