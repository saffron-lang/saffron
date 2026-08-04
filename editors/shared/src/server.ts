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
  DocumentSymbolParams,
  DocumentSymbol,
  SymbolKind,
  CompletionParams,
  CompletionItem,
  CompletionItemKind,
  DocumentFormattingParams,
  TextEdit,
  Range,
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { execFile } from "child_process";
import { promisify } from "util";
import * as path from "path";
import * as fs from "fs";
import {
  getBuiltinStub,
  getBuiltinFunctionLine,
  isBuiltinModule,
  getBuiltinFunctions,
  builtinModuleNames,
} from "./builtins";

const execFileAsync = promisify(execFile);

function findCompiler(): string {
  const serverDir = __dirname;
  const projectRoot = path.resolve(serverDir, "..", "..");
  const localBinary = path.join(projectRoot, "build", "saffronc");
  if (fs.existsSync(localBinary)) return localBinary;

  const homeBinary = path.join(process.env.HOME || "", ".saffron", "bin", "saffronc");
  if (fs.existsSync(homeBinary)) return homeBinary;

  return "saffronc";
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
    depPathCache = null;
    const rootBinary = path.join(rootPath, "build", "saffronc");
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
      documentSymbolProvider: true,
      completionProvider: {
        // `.` for member completion (Module.member), plus the default identifier
        // trigger the client applies on typing.
        triggerCharacters: ["."],
      },
      documentFormattingProvider: true,
    },
  };
});

// The compiler's --json diagnostic shape (see src/compiler/diag.sf
// diagnostic_to_json). line/column/offset/length are present ONLY when
// `located` is true; an unlocated diagnostic (most checker errors today) omits
// them rather than emitting 0, so the client must never place a squiggle for
// one — see mapDiagnostics.
interface SaffronDiagnostic {
  severity: "error" | "warning" | "internal compiler error";
  phase: string;
  file: string;
  message: string;
  located: boolean;
  line?: number;
  column?: number;
  offset?: number;
  length?: number;
}

// The --json symbol shape (see src/compiler/diag.sf symbol_json). Only located
// symbols are emitted, so line/column/offset/length are always present here.
interface SaffronSymbol {
  name: string;
  kind: "variable" | "function" | "class" | "parameter" | "module" | "enum" | "variant" | "interface" | "method";
  line: number;
  column: number;
  offset: number;
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

function getStubPath(moduleName: string): string {
  const stubDir = path.join(projectRoot, ".saffron", "stubs");
  return path.join(stubDir, moduleName + ".sf");
}

function ensureStub(moduleName: string): string | null {
  const stub = getBuiltinStub(moduleName);
  if (!stub) return null;

  const stubPath = getStubPath(moduleName);
  const stubDir = path.dirname(stubPath);
  if (!fs.existsSync(stubDir)) fs.mkdirSync(stubDir, { recursive: true });
  fs.writeFileSync(stubPath, stub);
  return stubPath;
}

function resolveImportPath(importPath: string, fromDir: string): string | null {
  if (importPath.startsWith("@")) {
    const name = importPath.slice(1);
    const libPath = path.join(projectRoot, "src", "lib", name + ".sf");
    if (fs.existsSync(libPath)) return libPath;
    return null;
  }

  if (importPath.startsWith("./") || importPath.startsWith("../")) {
    const resolved = path.resolve(fromDir, importPath);
    const withExt = resolved.endsWith(".sf") ? resolved : resolved + ".sf";
    if (fs.existsSync(withExt)) return withExt;
    return null;
  }

  // Package import: "turmeric/signal" → .pantry/packages/turmeric/src/signal.sf
  if (importPath.includes("/") && !importPath.startsWith(".")) {
    const parts = importPath.split("/");
    const pkgName = parts[0];
    const modulePath = parts.slice(1).join("/");

    // Try .pantry/packages first (installed deps)
    const pantryPkg = path.join(projectRoot, ".pantry", "packages", pkgName, "src", modulePath + ".sf");
    if (fs.existsSync(pantryPkg)) return pantryPkg;

    // Try direct path dependency from pantry.toml
    const depPath = getDepPath(pkgName);
    if (depPath) {
      const depModule = path.join(depPath, "src", modulePath + ".sf");
      if (fs.existsSync(depModule)) return depModule;
    }

    return null;
  }

  return null;
}

let depPathCache: Map<string, string> | null = null;

function getDepPath(pkgName: string): string | null {
  if (!depPathCache) {
    depPathCache = new Map();
    const tomlPath = path.join(projectRoot, "pantry.toml");
    if (fs.existsSync(tomlPath)) {
      const content = fs.readFileSync(tomlPath, "utf8");
      const depRegex = /^(\w[\w-]*)\s*=\s*\{\s*path\s*=\s*"([^"]+)"/gm;
      let m: RegExpExecArray | null;
      while ((m = depRegex.exec(content)) !== null) {
        depPathCache.set(m[1], path.resolve(projectRoot, m[2]));
      }
    }
  }
  return depPathCache.get(pkgName) || null;
}

function mapDiagnostics(raw: SaffronDiagnostic[]): Diagnostic[] {
  // Only located diagnostics become in-file squiggles. An unlocated one (the
  // compiler emits `located:false` for the many checker errors that have no
  // source region yet) has no defensible place to point — putting it at 0:0
  // would blame the top of the file for an error elsewhere. Those surface via
  // the message pane path instead (see publishDiagnostics), never as a range.
  return raw
    .filter((d) => d.located && d.line !== undefined && d.column !== undefined)
    .map((d) => {
      const line = d.line! - 1;
      const col = d.column! - 1;
      const len = Math.max(d.length ?? 1, 1);
      return {
        range: {
          start: { line, character: col },
          end: { line, character: col + len },
        },
        severity:
          d.severity === "warning"
            ? DiagnosticSeverity.Warning
            : DiagnosticSeverity.Error,
        message: d.message,
        source: "saffron",
      };
    });
}

// Compile a file (or the given in-memory text) in --json mode and parse the
// single JSON object the compiler prints. `text`, when supplied, is the unsaved
// editor buffer: it is written to a temp file so the compiler sees live edits
// before the user saves. Returns null only if the compiler produced no
// parseable JSON at all (a crash), which callers treat as "no info", not "clean".
async function runCheck(uri: string, text?: string): Promise<CheckOutput | null> {
  const realPath = decodeURIComponent(uri.replace("file://", ""));

  let checkPath = realPath;
  let tempPath: string | null = null;
  if (text !== undefined) {
    // Keep the .sf extension and basename so import resolution relative to the
    // file's own directory is unaffected; write alongside the real file.
    tempPath = path.join(
      path.dirname(realPath),
      `.${path.basename(realPath)}.lsp-${process.pid}.tmp.sf`,
    );
    try {
      fs.writeFileSync(tempPath, text);
      checkPath = tempPath;
    } catch {
      // Fall back to the on-disk file if the temp write fails (read-only dir).
      tempPath = null;
    }
  }

  const args = ["--json"];
  if (projectRoot) {
    const stdlibPath = path.join(projectRoot, "src", "lib");
    if (fs.existsSync(stdlibPath)) {
      args.push("--stdlib", stdlibPath);
    }
  }
  args.push(checkPath, "/dev/null");

  try {
    let output: string;
    try {
      const { stdout } = await execFileAsync(compilerPath, args, {
        timeout: 10000,
      });
      output = stdout;
    } catch (err: any) {
      // --json exits non-zero when the file has errors; the JSON is still on
      // stdout. Only stdout is JSON — stderr, if any, is not.
      output = err.stdout || "";
    }
    const result = parseCheckOutput(realPath, output);
    if (result) fileCache.set(uri, result);
    return result;
  } finally {
    if (tempPath) {
      try {
        fs.unlinkSync(tempPath);
      } catch {
        /* best effort */
      }
    }
  }
}

// Parse the compiler's --json object. The compiler reports diagnostics against
// the temp path when one is used, so `file` in the payload is overwritten with
// the real path the client knows. Returns null on unparseable output.
function parseCheckOutput(filePath: string, output: string): CheckOutput | null {
  const trimmed = output.trim();
  if (!trimmed) return null;
  let data: any;
  try {
    data = JSON.parse(trimmed);
  } catch {
    return null;
  }
  const diagnostics: SaffronDiagnostic[] = Array.isArray(data.diagnostics)
    ? data.diagnostics
    : [];
  const symbols: SaffronSymbol[] = Array.isArray(data.symbols) ? data.symbols : [];
  return { file: filePath, diagnostics, symbols };
}

interface WordContext {
  word: string;
  qualifier: string | null;
}

interface ImportContext {
  importPath: string;
}

function getImportAtPosition(doc: TextDocument, line: number, character: number): ImportContext | null {
  const text = doc.getText();
  const lines = text.split("\n");
  if (line >= lines.length) return null;
  const lineText = lines[line];

  const importRegex = /import\s+"([^"]+)"/g;
  const namedImportRegex = /import\s+\{[^}]+\}\s+from\s+"([^"]+)"/g;
  let match: RegExpExecArray | null;

  while ((match = importRegex.exec(lineText)) !== null) {
    const pathStart = match.index + match[0].indexOf('"') + 1;
    const pathEnd = pathStart + match[1].length;
    if (character >= pathStart && character <= pathEnd) {
      return { importPath: match[1] };
    }
  }

  while ((match = namedImportRegex.exec(lineText)) !== null) {
    const pathStart = match.index + match[0].lastIndexOf('"', match[0].length - 2) + 1;
    const pathEnd = pathStart + match[1].length;
    if (character >= pathStart && character <= pathEnd) {
      return { importPath: match[1] };
    }
  }

  return null;
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

  const importCtx = getImportAtPosition(doc, params.position.line, params.position.character);
  if (importCtx) {
    const filePath = decodeURIComponent(params.textDocument.uri.replace("file://", ""));
    const fileDir = path.dirname(filePath);
    const resolved = resolveImportPath(importCtx.importPath, fileDir);
    if (resolved) {
      return {
        uri: "file://" + resolved,
        range: { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } },
      };
    }
    if (isBuiltinModule(importCtx.importPath)) {
      const stubPath = ensureStub(importCtx.importPath);
      if (stubPath) {
        return {
          uri: "file://" + stubPath,
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } },
        };
      }
    }
    return null;
  }

  const ctx = getWordAtPosition(doc, params.position.line, params.position.character);
  if (!ctx) return null;

  let cached = fileCache.get(params.textDocument.uri);
  if (!cached) cached = await runCheck(params.textDocument.uri) ?? undefined;
  if (!cached) return null;

  // Member access: Iter.map → look up "map" in Iter's source file
  if (ctx.qualifier) {
    // Check if qualifier is a builtin module (IO, Task, etc.)
    if (isBuiltinModule(ctx.qualifier)) {
      const stubPath = ensureStub(ctx.qualifier);
      if (stubPath) {
        const line = getBuiltinFunctionLine(ctx.qualifier, ctx.word);
        if (line !== null) {
          return {
            uri: "file://" + stubPath,
            range: {
              start: { line, character: 4 },
              end: { line, character: 4 + ctx.word.length },
            },
          };
        }
        return {
          uri: "file://" + stubPath,
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } },
        };
      }
    }

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
  // If word is a module alias, navigate to source or stub
  const imports = resolveImports(doc);
  const targetFile = imports.get(ctx.word);
  if (targetFile) {
    return {
      uri: "file://" + targetFile,
      range: { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } },
    };
  }

  // Check if it's a builtin module name used directly
  if (isBuiltinModule(ctx.word)) {
    const stubPath = ensureStub(ctx.word);
    if (stubPath) {
      return {
        uri: "file://" + stubPath,
        range: { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } },
      };
    }
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

// Check the given document and publish its diagnostics. `text` passes the live
// (possibly unsaved) buffer through to the compiler via a temp file.
async function checkAndPublish(doc: TextDocument): Promise<void> {
  const result = await runCheck(doc.uri, doc.getText());
  // Only replace a prior clean/located result on a parseable payload. A null
  // (compiler crash) leaves the last good diagnostics in place rather than
  // flickering the squiggles off.
  const diagnostics = result ? mapDiagnostics(result.diagnostics) : [];
  if (result) {
    connection.sendDiagnostics({ uri: doc.uri, diagnostics });
  }
}

// Debounce live checks so a burst of keystrokes triggers one compile, not one
// per character. 400ms is long enough to coalesce typing, short enough to feel
// live. Keyed by uri so edits to different files don't cancel each other.
const debounceTimers = new Map<string, NodeJS.Timeout>();
const DEBOUNCE_MS = 400;

function scheduleCheck(doc: TextDocument): void {
  const existing = debounceTimers.get(doc.uri);
  if (existing) clearTimeout(existing);
  debounceTimers.set(
    doc.uri,
    setTimeout(() => {
      debounceTimers.delete(doc.uri);
      void checkAndPublish(doc);
    }, DEBOUNCE_MS),
  );
}

documents.onDidChangeContent((event) => {
  // Fires on open and on every edit (TextDocumentSyncKind.Full). Debounced so
  // live typing produces one compile per pause, giving live diagnostics that
  // onDidSave alone could not.
  scheduleCheck(event.document);
});

documents.onDidSave(async (event) => {
  const existing = debounceTimers.get(event.document.uri);
  if (existing) {
    clearTimeout(existing);
    debounceTimers.delete(event.document.uri);
  }
  await checkAndPublish(event.document);
});

documents.onDidClose((event) => {
  const existing = debounceTimers.get(event.document.uri);
  if (existing) {
    clearTimeout(existing);
    debounceTimers.delete(event.document.uri);
  }
  fileCache.delete(event.document.uri);
  connection.sendDiagnostics({ uri: event.document.uri, diagnostics: [] });
});

// --- Document outline (documentSymbol) ---

const symbolKinds: Record<string, SymbolKind> = {
  variable: SymbolKind.Variable,
  function: SymbolKind.Function,
  class: SymbolKind.Class,
  parameter: SymbolKind.Variable,
  module: SymbolKind.Module,
  enum: SymbolKind.Enum,
  variant: SymbolKind.EnumMember,
  interface: SymbolKind.Interface,
  method: SymbolKind.Method,
};

function symbolRange(sym: SaffronSymbol) {
  const line = sym.line - 1;
  const col = sym.column - 1;
  return {
    start: { line, character: col },
    end: { line, character: col + sym.length },
  };
}

connection.onDocumentSymbol(
  async (params: DocumentSymbolParams): Promise<DocumentSymbol[]> => {
    const doc = documents.get(params.textDocument.uri);
    if (!doc) return [];

    let cached = fileCache.get(params.textDocument.uri);
    if (!cached) cached = (await runCheck(params.textDocument.uri, doc.getText())) ?? undefined;
    if (!cached) return [];

    // The compiler emits a flat symbol list (declarations plus enum variants and
    // class methods). Present it flat; a nesting pass would need container info
    // the payload does not yet carry. Each symbol's range and selectionRange are
    // the same name-span — precise enough for outline navigation and highlight.
    return cached.symbols.map((sym) => {
      const range = symbolRange(sym);
      return {
        name: sym.name,
        kind: symbolKinds[sym.kind] ?? SymbolKind.Variable,
        range,
        selectionRange: range,
      };
    });
  },
);

// --- Completion ---

const completionKinds: Record<string, CompletionItemKind> = {
  variable: CompletionItemKind.Variable,
  function: CompletionItemKind.Function,
  class: CompletionItemKind.Class,
  parameter: CompletionItemKind.Variable,
  module: CompletionItemKind.Module,
  enum: CompletionItemKind.Enum,
  variant: CompletionItemKind.EnumMember,
  interface: CompletionItemKind.Interface,
  method: CompletionItemKind.Method,
};

connection.onCompletion(
  async (params: CompletionParams): Promise<CompletionItem[]> => {
    const doc = documents.get(params.textDocument.uri);
    if (!doc) return [];

    const ctx = getWordAtPosition(doc, params.position.line, params.position.character);

    // Member completion: `Module.<here>` or `alias.<here>`. Offer the members of
    // the referenced module — a builtin's known functions, or the symbols of an
    // imported .sf file.
    if (ctx?.qualifier) {
      if (isBuiltinModule(ctx.qualifier)) {
        return getBuiltinFunctions(ctx.qualifier).map((fn) => ({
          label: fn.name,
          kind: CompletionItemKind.Function,
          detail: `fun ${fn.name}(${fn.params}): ${fn.returnType}`,
        }));
      }
      const imports = resolveImports(doc);
      const targetFile = imports.get(ctx.qualifier);
      if (targetFile) {
        const targetUri = "file://" + targetFile;
        let targetCheck = fileCache.get(targetUri);
        if (!targetCheck) targetCheck = (await runCheck(targetUri)) ?? undefined;
        if (targetCheck) {
          // Only top-level, public-facing kinds are meaningful as module members.
          return targetCheck.symbols
            .filter((s) => s.kind === "function" || s.kind === "class" || s.kind === "enum" || s.kind === "interface" || s.kind === "variable")
            .map((s) => ({
              label: s.name,
              kind: completionKinds[s.kind] ?? CompletionItemKind.Field,
            }));
        }
      }
      return [];
    }

    // Bare identifier completion: this file's own declarations plus the builtin
    // module names (so typing `IO` offers the module to dot into).
    let cached = fileCache.get(params.textDocument.uri);
    if (!cached) cached = (await runCheck(params.textDocument.uri, doc.getText())) ?? undefined;

    const items: CompletionItem[] = [];
    if (cached) {
      const seen = new Set<string>();
      for (const s of cached.symbols) {
        if (seen.has(s.name)) continue;
        seen.add(s.name);
        items.push({ label: s.name, kind: completionKinds[s.kind] ?? CompletionItemKind.Variable });
      }
    }
    for (const name of builtinModuleNames) {
      items.push({ label: name, kind: CompletionItemKind.Module });
    }
    return items;
  },
);

// --- Formatting --------------------------------------------------------------
//
// Formats the OPEN BUFFER and returns edits. It deliberately does not invoke
// `saffronc format --write`:
//
//   * --write reformats the file on disk, which is not what the editor asked
//     for. The buffer is usually ahead of disk (format-on-save runs before the
//     write in most clients), so --write would format stale text and then the
//     editor would overwrite it with the unformatted buffer.
//   * Writing behind the editor's back also loses the undo entry. An edit the
//     client applies is undoable with one keystroke; a file mutated underneath
//     it is not.
//
// So the buffer text goes in through a temp file and the formatted text comes
// back on stdout, and the result is handed to the client as a single
// whole-document replacement. `format` writes nothing without --write, which is
// exactly the default this relies on.
//
// A single full-range edit rather than a computed minimal diff: the formatter
// only ever changes whitespace, so a full replacement is semantically identical
// to the minimal set, and LSP clients preserve the cursor across it. Computing a
// real diff would be strictly more code for the same visible behaviour.
async function formatBuffer(uri: string, text: string): Promise<string | null> {
  const realPath = decodeURIComponent(uri.replace("file://", ""));
  // Beside the real file and keeping the .sf extension, same as runCheck: the
  // formatter resolves no imports, but a .sf name keeps the compiler's own
  // extension check happy and the location keeps behaviour identical if that
  // ever changes.
  const tempPath = path.join(
    path.dirname(realPath),
    `.${path.basename(realPath)}.fmt-${process.pid}.tmp.sf`,
  );
  try {
    fs.writeFileSync(tempPath, text);
  } catch {
    // Read-only directory. Formatting a buffer we cannot stage is a no-op rather
    // than an error dialog; the user did not ask about the filesystem.
    return null;
  }
  try {
    const { stdout } = await execFileAsync(
      compilerPath,
      ["format", tempPath],
      { timeout: 10000 },
    );
    return stdout;
  } catch (err: any) {
    // A compiler too old to know the `format` subcommand, or any other failure.
    // Return null so the client keeps the buffer untouched: silently leaving
    // text alone is the only safe failure mode for a formatter.
    connection.console.log(
      `Saffron LSP: format failed (${err?.message ?? "unknown error"})`,
    );
    return null;
  } finally {
    try {
      fs.unlinkSync(tempPath);
    } catch {
      /* best effort */
    }
  }
}

connection.onDocumentFormatting(
  async (params: DocumentFormattingParams): Promise<TextEdit[]> => {
    const doc = documents.get(params.textDocument.uri);
    if (!doc) return [];

    const original = doc.getText();
    const formatted = await formatBuffer(params.textDocument.uri, original);
    // No edit when formatting failed OR when the text is already formatted.
    // Returning an identical edit would mark the document dirty and, under
    // format-on-save, make every save of an already-formatted file a change.
    if (formatted === null || formatted === original) return [];

    const fullRange: Range = {
      start: { line: 0, character: 0 },
      end: doc.positionAt(original.length),
    };
    return [TextEdit.replace(fullRange, formatted)];
  },
);

documents.listen(connection);
connection.listen();
