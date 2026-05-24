#!/usr/bin/env node
import {
  createConnection,
  TextDocuments,
  Diagnostic,
  DiagnosticSeverity,
  ProposedFeatures,
  InitializeParams,
  InitializeResult,
  TextDocumentSyncKind,
  DefinitionParams,
  HoverParams,
  Hover,
  Location,
  MarkupKind,
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { execFile } from "child_process";
import { promisify } from "util";
import * as path from "path";
import * as fs from "fs";

const execFileAsync = promisify(execFile);

function findCompiler(): string {
  const serverDir = __dirname;
  const projectRoot = path.resolve(serverDir, "..", "..");
  const localBinary = path.join(projectRoot, "cmake-build-debug", "saffron");
  if (fs.existsSync(localBinary)) return localBinary;

  const homeBinary = path.join(process.env.HOME || "", ".saffron", "bin", "saffron");
  if (fs.existsSync(homeBinary)) return homeBinary;

  return "saffron";
}

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

let compilerPath = findCompiler();

connection.onInitialize((params: InitializeParams): InitializeResult => {
  const settings = params.initializationOptions;
  if (settings?.compilerPath) {
    compilerPath = settings.compilerPath;
  }

  if (params.rootUri) {
    const rootPath = decodeURIComponent(params.rootUri.replace("file://", ""));
    const rootBinary = path.join(rootPath, "cmake-build-debug", "saffron");
    if (!settings?.compilerPath && fs.existsSync(rootBinary)) {
      compilerPath = rootBinary;
    }
  }

  connection.console.log(`Saffron LSP using compiler: ${compilerPath}`);
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Full,
      hoverProvider: true,
      definitionProvider: true,
    },
  };
});

interface SaffronDiagnostic {
  line: number;
  column: number;
  length: number;
  severity: "error" | "warning";
  message: string;
}

interface SaffronSymbol {
  name: string;
  kind: "variable" | "function" | "class" | "parameter";
  line: number;
  column: number;
  length: number;
}

interface CheckOutput {
  file: string;
  diagnostics: SaffronDiagnostic[];
  symbols: SaffronSymbol[];
}

const fileCache = new Map<string, CheckOutput>();

function mapDiagnostics(raw: SaffronDiagnostic[]): Diagnostic[] {
  return raw.map((d) => ({
    range: {
      start: { line: d.line - 1, character: d.column - 1 },
      end: { line: d.line - 1, character: d.column - 1 + Math.max(d.length, 1) },
    },
    severity:
      d.severity === "error"
        ? DiagnosticSeverity.Error
        : DiagnosticSeverity.Warning,
    message: d.message,
    source: "saffron",
  }));
}

async function runCheck(uri: string): Promise<CheckOutput | null> {
  const filePath = decodeURIComponent(uri.replace("file://", ""));

  try {
    const { stdout } = await execFileAsync(compilerPath, ["--check", filePath], {
      timeout: 10000,
    });
    const result: CheckOutput = JSON.parse(stdout);
    fileCache.set(uri, result);
    return result;
  } catch (err: any) {
    if (err.stdout) {
      try {
        const result: CheckOutput = JSON.parse(err.stdout);
        fileCache.set(uri, result);
        return result;
      } catch {
        // JSON parse failed
      }
    }
    return null;
  }
}

function getWordAtPosition(doc: TextDocument, line: number, character: number): string | null {
  const text = doc.getText();
  const lines = text.split("\n");
  if (line >= lines.length) return null;
  const lineText = lines[line];

  let start = character;
  while (start > 0 && /[a-zA-Z0-9_?!]/.test(lineText[start - 1])) start--;
  let end = character;
  while (end < lineText.length && /[a-zA-Z0-9_?!]/.test(lineText[end])) end++;

  if (start === end) return null;
  return lineText.slice(start, end);
}

function findSymbol(symbols: SaffronSymbol[], name: string): SaffronSymbol | null {
  for (let i = symbols.length - 1; i >= 0; i--) {
    if (symbols[i].name === name) return symbols[i];
  }
  return null;
}

connection.onHover(async (params: HoverParams): Promise<Hover | null> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;

  const word = getWordAtPosition(doc, params.position.line, params.position.character);
  if (!word) return null;

  let cached = fileCache.get(params.textDocument.uri);
  if (!cached) cached = await runCheck(params.textDocument.uri) ?? undefined;
  if (!cached) return null;

  const sym = findSymbol(cached.symbols, word);
  if (!sym) return null;

  const kindLabel = sym.kind === "function" ? "fun" : sym.kind === "class" ? "class" : "var";
  return {
    contents: {
      kind: MarkupKind.Markdown,
      value: `\`\`\`saffron\n(${kindLabel}) ${sym.name}\n\`\`\``,
    },
  };
});

connection.onDefinition(async (params: DefinitionParams): Promise<Location | null> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;

  const word = getWordAtPosition(doc, params.position.line, params.position.character);
  if (!word) return null;

  let cached = fileCache.get(params.textDocument.uri);
  if (!cached) cached = await runCheck(params.textDocument.uri) ?? undefined;
  if (!cached) return null;

  const sym = findSymbol(cached.symbols, word);
  if (!sym) return null;

  return {
    uri: params.textDocument.uri,
    range: {
      start: { line: sym.line - 1, character: sym.column - 1 },
      end: { line: sym.line - 1, character: sym.column - 1 + sym.length },
    },
  };
});

documents.onDidSave(async (event) => {
  const result = await runCheck(event.document.uri);
  const diagnostics = result ? mapDiagnostics(result.diagnostics) : [];
  connection.sendDiagnostics({ uri: event.document.uri, diagnostics });
});

documents.onDidOpen(async (event) => {
  const result = await runCheck(event.document.uri);
  const diagnostics = result ? mapDiagnostics(result.diagnostics) : [];
  connection.sendDiagnostics({ uri: event.document.uri, diagnostics });
});

documents.onDidClose((event) => {
  fileCache.delete(event.document.uri);
  connection.sendDiagnostics({ uri: event.document.uri, diagnostics: [] });
});

documents.listen(connection);
connection.listen();
