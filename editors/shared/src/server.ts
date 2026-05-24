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
    projectRoot = rootPath;
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
  kind: "variable" | "function" | "class" | "parameter" | "module" | "enum" | "variant" | "interface" | "method";
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
let projectRoot = "";

interface ModuleImport {
  alias: string;
  filePath: string | null;
}

function resolveImports(doc: TextDocument): Map<string, string> {
  const imports = new Map<string, string>();
  const text = doc.getText();
  const filePath = decodeURIComponent(doc.uri.replace("file://", ""));
  const fileDir = path.dirname(filePath);

  const importRegex = /import\s+"([^"]+)"\s+as\s+(\w+)/g;
  const namedImportRegex = /import\s+\{[^}]+\}\s+from\s+"([^"]+)"/g;
  let match: RegExpExecArray | null;

  while ((match = importRegex.exec(text)) !== null) {
    const importPath = match[1];
    const alias = match[2];
    const resolved = resolveImportPath(importPath, fileDir);
    if (resolved) imports.set(alias, resolved);
  }

  while ((match = namedImportRegex.exec(text)) !== null) {
    const importPath = match[1];
    const resolved = resolveImportPath(importPath, fileDir);
    if (resolved) imports.set(importPath, resolved);
  }

  return imports;
}

function resolveImportPath(importPath: string, fromDir: string): string | null {
  if (importPath.startsWith("@")) {
    const name = importPath.slice(1);
    const libPath = path.join(projectRoot, "src", "lib", name + ".sf");
    if (fs.existsSync(libPath)) return libPath;
    const buildLibPath = path.join(projectRoot, "cmake-build-debug", "lib", name + ".sf");
    if (fs.existsSync(buildLibPath)) return buildLibPath;
    return null;
  }

  if (importPath.startsWith("./") || importPath.startsWith("../")) {
    const resolved = path.resolve(fromDir, importPath);
    const withExt = resolved.endsWith(".sf") ? resolved : resolved + ".sf";
    if (fs.existsSync(withExt)) return withExt;
    return null;
  }

  // Builtin C module (time, json, etc.) — no source file
  return null;
}

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

interface WordContext {
  word: string;
  qualifier: string | null;
}

function getWordAtPosition(doc: TextDocument, line: number, character: number): WordContext | null {
  const text = doc.getText();
  const lines = text.split("\n");
  if (line >= lines.length) return null;
  const lineText = lines[line];

  let start = character;
  while (start > 0 && /[a-zA-Z0-9_?!]/.test(lineText[start - 1])) start--;
  let end = character;
  while (end < lineText.length && /[a-zA-Z0-9_?!]/.test(lineText[end])) end++;

  if (start === end) return null;
  const word = lineText.slice(start, end);

  let qualifier: string | null = null;
  if (start > 0 && lineText[start - 1] === ".") {
    let qStart = start - 2;
    while (qStart > 0 && /[a-zA-Z0-9_]/.test(lineText[qStart - 1])) qStart--;
    if (qStart < start - 1) {
      qualifier = lineText.slice(qStart, start - 1);
    }
  }

  return { word, qualifier };
}

function findSymbol(symbols: SaffronSymbol[], name: string, qualifier: string | null): SaffronSymbol | null {
  if (qualifier) {
    for (let i = symbols.length - 1; i >= 0; i--) {
      if (symbols[i].name === name) return symbols[i];
    }
  }
  for (let i = symbols.length - 1; i >= 0; i--) {
    if (symbols[i].name === name) return symbols[i];
  }
  return null;
}

function findDefinitionInFile(filePath: string, name: string): { start: { line: number; character: number }; end: { line: number; character: number } } | null {
  let content: string;
  try {
    content = fs.readFileSync(filePath, "utf8");
  } catch {
    return null;
  }

  const lines = content.split("\n");
  const pattern = new RegExp(`\\b(fun|var|class|enum|interface)\\s+${name.replace(/[?!]/g, "\\$&")}\\b`);
  for (let i = 0; i < lines.length; i++) {
    const match = pattern.exec(lines[i]);
    if (match) {
      const col = match.index + match[1].length + 1;
      return {
        start: { line: i, character: col },
        end: { line: i, character: col + name.length },
      };
    }
  }
  return null;
}

const kindLabels: Record<string, string> = {
  variable: "var",
  function: "fun",
  class: "class",
  parameter: "param",
  module: "module",
  enum: "enum",
  variant: "variant",
  interface: "interface",
  method: "fun",
};

connection.onHover(async (params: HoverParams): Promise<Hover | null> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;

  const ctx = getWordAtPosition(doc, params.position.line, params.position.character);
  if (!ctx) return null;

  let cached = fileCache.get(params.textDocument.uri);
  if (!cached) cached = await runCheck(params.textDocument.uri) ?? undefined;
  if (!cached) return null;

  const sym = findSymbol(cached.symbols, ctx.word, ctx.qualifier);
  if (!sym) return null;

  const label = kindLabels[sym.kind] || sym.kind;
  const prefix = ctx.qualifier ? `${ctx.qualifier}.` : "";
  return {
    contents: {
      kind: MarkupKind.Markdown,
      value: `\`\`\`saffron\n(${label}) ${prefix}${sym.name}\n\`\`\``,
    },
  };
});

connection.onDefinition(async (params: DefinitionParams): Promise<Location | null> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;

  const ctx = getWordAtPosition(doc, params.position.line, params.position.character);
  if (!ctx) return null;

  let cached = fileCache.get(params.textDocument.uri);
  if (!cached) cached = await runCheck(params.textDocument.uri) ?? undefined;
  if (!cached) return null;

  // Member access: Iter.map → look up "map" in Iter's source file
  if (ctx.qualifier) {
    const imports = resolveImports(doc);
    const targetFile = imports.get(ctx.qualifier);
    if (targetFile) {
      const targetUri = "file://" + targetFile;
      // Try compiler check first
      let targetCheck = fileCache.get(targetUri);
      if (!targetCheck) targetCheck = await runCheck(targetUri) ?? undefined;
      if (targetCheck) {
        const memberSym = findSymbol(targetCheck.symbols, ctx.word, null);
        if (memberSym) {
          return {
            uri: targetUri,
            range: {
              start: { line: memberSym.line - 1, character: memberSym.column - 1 },
              end: { line: memberSym.line - 1, character: memberSym.column - 1 + memberSym.length },
            },
          };
        }
      }
      // Fallback: scan file text for definition
      const loc = findDefinitionInFile(targetFile, ctx.word);
      if (loc) {
        return { uri: targetUri, range: loc };
      }
    }
    // Fall through: navigate to the module/enum declaration itself
    const sym = findSymbol(cached.symbols, ctx.qualifier, null);
    if (sym) {
      return {
        uri: params.textDocument.uri,
        range: {
          start: { line: sym.line - 1, character: sym.column - 1 },
          end: { line: sym.line - 1, character: sym.column - 1 + sym.length },
        },
      };
    }
    return null;
  }

  // No qualifier: look up in current file
  // If word is a module alias, navigate to source file
  const imports = resolveImports(doc);
  const targetFile = imports.get(ctx.word);
  if (targetFile) {
    return {
      uri: "file://" + targetFile,
      range: { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } },
    };
  }

  const sym = findSymbol(cached.symbols, ctx.word, null);
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
