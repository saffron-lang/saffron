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
  Position,
  DocumentSymbolParams,
  DocumentSymbol,
  SymbolKind,
  CompletionParams,
  CompletionItem,
  CompletionItemKind,
  DocumentFormattingParams,
  TextEdit,
  Range,
  ReferenceParams,
  RenameParams,
  PrepareRenameParams,
  WorkspaceEdit,
  DocumentHighlight,
  DocumentHighlightKind,
  DocumentHighlightParams,
  FoldingRange,
  FoldingRangeKind,
  FoldingRangeParams,
  SignatureHelp,
  SignatureHelpParams,
  SignatureInformation,
  ParameterInformation,
  SemanticTokensParams,
  SemanticTokens,
  SemanticTokensLegend,
  WorkspaceSymbolParams,
  WorkspaceSymbol,
  ResponseError,
  ErrorCodes,
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { execFile } from "child_process";
import { promisify } from "util";
import * as path from "path";
import * as fs from "fs";
import {
  scanIdentifiers,
  occurrencesOf,
  splitParams,
  callContextAt,
  IDENT_START,
  IDENT_PART,
  Occurrence,
} from "./scan";
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

// The semantic-token types this server can produce, and nothing more. A client
// indexes tokens by their position in this legend, so the order is part of the
// wire format: appending is safe, reordering silently recolours everything.
//
// Only types the compiler can actually justify are listed. A legend advertising
// `keyword` or `operator` would be a promise to classify them, and those come
// from the TextMate grammar, which already does it without a round-trip to the
// compiler. Semantic tokens here exist to say what the grammar *cannot* know:
// whether `Foo` is a class, an enum or a variable.
const semanticTokenTypes = [
  "class",
  "interface",
  "enum",
  "enumMember",
  "function",
  "method",
  "property",
  "variable",
  "parameter",
] as const;

// Modifiers are a bitset, so their order is wire format too. `declaration`
// marks the defining occurrence; the three visibility modifiers let a theme
// distinguish a private member without the client parsing anything.
const semanticTokenModifiers = [
  "declaration",
  "private",
  "protected",
  "internal",
] as const;

const semanticTokensLegend: SemanticTokensLegend = {
  tokenTypes: semanticTokenTypes as unknown as string[],
  tokenModifiers: semanticTokenModifiers as unknown as string[],
};

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
      referencesProvider: true,
      documentHighlightProvider: true,
      foldingRangeProvider: true,
      workspaceSymbolProvider: true,
      // prepareProvider makes the client ask whether a rename is legal at the
      // cursor *before* prompting for the new name. Without it the user types a
      // replacement and only then learns the position was not renameable.
      renameProvider: { prepareProvider: true },
      signatureHelpProvider: {
        triggerCharacters: ["("],
        retriggerCharacters: [","],
      },
      semanticTokensProvider: {
        legend: semanticTokensLegend,
        full: true,
        // No `range` and no delta support: this server recomputes tokens from a
        // whole-file compile anyway, so a range request would do identical work
        // and return a subset. Advertising only what is actually incremental
        // keeps the client from expecting cheap partial updates that aren't.
      },
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
//
// `line`/`column`/`length` describe the NAME only — the rename and highlight
// target. `start_offset`/`end_offset` describe the declaration's full extent and
// are what folding uses; they are absent when the node had no real extent span, so
// a folding range must be skipped rather than invented for those.
interface SaffronSymbol {
  name: string;
  kind: SaffronSymbolKind;
  line: number;
  column: number;
  offset: number;
  length: number;
  /** Declared access modifier. Always present; the parser defaults it to "public". */
  visibility: "public" | "private" | "internal" | "protected";
  /** Owning declaration's name for a member (method/field/variant); "" at top level. */
  container: string;
  /** Rendered signature or type for display. "" when there is nothing to show. */
  detail: string;
  /** Doc comment with parser markers stripped; "" when undocumented. */
  doc: string;
  /** Full-extent byte offsets. Absent when the declaration has no real extent. */
  start_offset?: number;
  end_offset?: number;
}

type SaffronSymbolKind =
  | "variable"
  | "function"
  | "class"
  | "parameter"
  | "module"
  | "enum"
  | "variant"
  | "interface"
  | "method"
  | "field"
  | "actor";

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

/** True when every character in `s` is one UTF-8 byte and one UTF-16 unit. */
function isAscii(s: string): boolean {
  for (let i = 0; i < s.length; i++) {
    if (s.charCodeAt(i) > 0x7f) return false;
  }
  return true;
}

/**
 * Convert the compiler's 1-based BYTE column into an LSP `character`.
 *
 * The two are not the same unit, and nothing here was accounting for it.
 * `AST.Span` documents `offset`/`len` as byte offsets and the lexer counts `col`
 * in bytes to match; LSP positions are UTF-16 code units. So every byte a
 * multi-byte character contributes beyond its UTF-16 width shifts the range
 * right: three `é` (2 bytes each, 1 unit each) before the error put the squiggle
 * 3 columns past the token it was about.
 *
 * ASCII lines are the overwhelming majority and cost nothing here: the fast path
 * returns immediately when the line has no byte above 0x7F, so this only does
 * work on the lines that actually need it.
 *
 * `lineText` is the raw line from the document. When it is unavailable the byte
 * column is returned unchanged, which is exactly the old behaviour and correct
 * for ASCII — a wrong-but-close range still beats dropping the diagnostic.
 */
function byteColumnToCharacter(lineText: string | undefined, byteCol: number): number {
  const zero = Math.max(byteCol - 1, 0);
  if (lineText === undefined || isAscii(lineText)) return zero;
  // Walk characters, accumulating each one's UTF-8 width, until the byte budget
  // is spent. `for...of` iterates by code POINT, so an astral character (4 UTF-8
  // bytes, 2 UTF-16 units) advances both counters correctly, which a charCodeAt
  // loop would get wrong on the surrogate pair.
  let bytes = 0;
  let units = 0;
  for (const ch of lineText) {
    if (bytes >= zero) break;
    bytes += Buffer.byteLength(ch, "utf8");
    units += ch.length;
  }
  return units;
}

/** The raw text of a 0-based line, or undefined if the line is out of range. */
function lineTextAt(doc: TextDocument | undefined, line: number): string | undefined {
  if (!doc) return undefined;
  const lines = doc.getText().split("\n");
  return line < lines.length ? lines[line] : undefined;
}

/**
 * A range from a 1-based line, 1-based byte column and byte length.
 *
 * The END is computed by converting `byteCol + byteLen` rather than by adding a
 * converted length to a converted start: the length is a byte count too, and
 * adding it to a UTF-16 start silently widens the range over any multi-byte
 * character inside the token (`café` is 5 bytes, 4 units).
 */
function byteRange(
  doc: TextDocument | undefined,
  line1: number,
  col1: number,
  byteLen: number,
) {
  const line = line1 - 1;
  const text = lineTextAt(doc, line);
  return {
    start: { line, character: byteColumnToCharacter(text, col1) },
    end: { line, character: byteColumnToCharacter(text, col1 + Math.max(byteLen, 1)) },
  };
}

/**
 * An LSP Position from one of the compiler's whole-file BYTE offsets.
 *
 * `doc.positionAt` takes a JS string index (UTF-16 units), so handing it
 * `sym.start_offset` directly is the same unit error as the column one above —
 * just measured from the start of the file instead of the start of a line, so it
 * accumulates over every multi-byte character ABOVE the declaration, not merely
 * the ones to its left. A doc comment with an en-dash in it is enough to shift
 * the fold and the outline extent of everything below it.
 *
 * NOTE the asymmetry with the offsets in `scan.ts` (`occ.offset`), which are
 * already string indices because the scanner walks the JS string. Those go to
 * `positionAt` unconverted and correctly; only compiler-sourced offsets pass
 * through here.
 */
function positionAtByteOffset(doc: TextDocument, byteOffset: number): Position {
  const text = doc.getText();
  // Fast path, and it is the usual one: with no byte above 0x7F the two units
  // coincide exactly. Worth checking because onFoldingRange calls this twice per
  // symbol, and the slow path below is a full walk of the document each time.
  if (isAscii(text)) return doc.positionAt(byteOffset);
  let bytes = 0;
  let units = 0;
  for (const ch of text) {
    if (bytes >= byteOffset) break;
    bytes += Buffer.byteLength(ch, "utf8");
    units += ch.length;
  }
  return doc.positionAt(units);
}

function mapDiagnostics(raw: SaffronDiagnostic[], doc?: TextDocument): Diagnostic[] {
  // Only located diagnostics become in-file squiggles. An unlocated one (the
  // compiler emits `located:false` for the many checker errors that have no
  // source region yet) has no defensible place to point — putting it at 0:0
  // would blame the top of the file for an error elsewhere. Those surface via
  // the message pane path instead (see publishDiagnostics), never as a range.
  return raw
    .filter((d) => d.located && d.line !== undefined && d.column !== undefined)
    .map((d) => {
      return {
        // `doc` is optional and absent only where no buffer is open; byteRange
        // then falls back to the byte column, which is exact for ASCII.
        range: byteRange(doc, d.line!, d.column!, d.length ?? 1),
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
  // Normalize the symbol list rather than trusting it wholesale. A compiler older
  // than the payload extension omits visibility/container/detail/doc, and every
  // consumer below would otherwise have to guard each field independently — the
  // kind of scattered `?? ""` that makes a missing field indistinguishable from an
  // empty one at the point it matters. Defaulting once here keeps the rest honest.
  const symbols: SaffronSymbol[] = (Array.isArray(data.symbols) ? data.symbols : []).map(
    (s: any): SaffronSymbol => ({
      ...s,
      visibility: s.visibility ?? "public",
      container: s.container ?? "",
      detail: s.detail ?? "",
      doc: s.doc ?? "",
    }),
  );
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
  field: "var",
  actor: "actor",
};

/**
 * A declaration rendered as the source line it came from, near enough to read.
 *
 * The visibility modifier is shown only when it is not the default. Printing
 * `public` on every symbol would put a modifier on hover text that the author
 * never wrote, and the interesting case — that a member is `private` — would
 * stop standing out.
 */
function renderDeclaration(sym: SaffronSymbol): string {
  const label = kindLabels[sym.kind] ?? sym.kind;
  const vis = sym.visibility === "public" ? "" : `${sym.visibility} `;
  const qualified = sym.container.length > 0 ? `${sym.container}.${sym.name}` : sym.name;

  // detail is either a rendered signature — starting with `(` — or a bare type.
  // A signature juxtaposes; a type needs its `:`.
  if (sym.detail.startsWith("(")) return `${vis}${label} ${qualified}${sym.detail}`;
  if (sym.detail.length > 0) return `${vis}${label} ${qualified}: ${sym.detail}`;
  return `${vis}${label} ${qualified}`;
}

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

  // The declaration first, in a code fence, then the doc comment as prose below
  // it — the conventional shape, and the reason `doc` is in the payload.
  let value = `\`\`\`saffron\n${renderDeclaration(sym)}\n\`\`\``;
  if (sym.doc.length > 0) value += `\n\n${sym.doc}`;
  return { contents: { kind: MarkupKind.Markdown, value } };
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
        range: symbolRange(sym, doc),
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
    range: symbolRange(sym, doc),
  };
});

// Check the given document and publish its diagnostics. `text` passes the live
// (possibly unsaved) buffer through to the compiler via a temp file.
async function checkAndPublish(doc: TextDocument): Promise<void> {
  const result = await runCheck(doc.uri, doc.getText());
  // Only replace a prior clean/located result on a parseable payload. A null
  // (compiler crash) leaves the last good diagnostics in place rather than
  // flickering the squiggles off.
  const diagnostics = result ? mapDiagnostics(result.diagnostics, doc) : [];
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
  field: SymbolKind.Field,
  // No SymbolKind.Actor exists. Class is the closest true statement — an actor
  // is a class with serialized methods — and `detail` says "actor" in the text.
  actor: SymbolKind.Class,
};

function symbolRange(sym: SaffronSymbol, doc?: TextDocument) {
  return byteRange(doc, sym.line, sym.column, sym.length);
}

connection.onDocumentSymbol(
  async (params: DocumentSymbolParams): Promise<DocumentSymbol[]> => {
    const doc = documents.get(params.textDocument.uri);
    if (!doc) return [];

    let cached = fileCache.get(params.textDocument.uri);
    if (!cached) cached = (await runCheck(params.textDocument.uri, doc.getText())) ?? undefined;
    if (!cached) return [];

    // Nested from the payload's `container`: a method or field hangs under its
    // class, a variant under its enum. `container` is a *name*, so two classes
    // in one file each declaring `init` both nest correctly, which a flat list
    // could not express and a line-range heuristic would get wrong for a nested
    // declaration.
    const toSymbol = (sym: SaffronSymbol): DocumentSymbol => {
      const selectionRange = symbolRange(sym, doc);
      // range must CONTAIN selectionRange or clients drop the entry. The full
      // extent satisfies that when present; the name span trivially does when not.
      const range =
        sym.start_offset !== undefined && sym.end_offset !== undefined && doc
          ? {
              start: positionAtByteOffset(doc, sym.start_offset),
              end: positionAtByteOffset(doc, sym.end_offset),
            }
          : selectionRange;
      const out: DocumentSymbol = {
        name: sym.name,
        kind: symbolKinds[sym.kind] ?? SymbolKind.Variable,
        range,
        selectionRange,
      };
      if (sym.detail.length > 0) out.detail = sym.detail;
      return out;
    };

    const byContainer = new Map<string, SaffronSymbol[]>();
    for (const sym of cached.symbols) {
      if (sym.container.length === 0) continue;
      const bucket = byContainer.get(sym.container);
      if (bucket) bucket.push(sym);
      else byContainer.set(sym.container, [sym]);
    }

    const roots: DocumentSymbol[] = [];
    for (const sym of cached.symbols) {
      if (sym.container.length > 0) continue;
      const node = toSymbol(sym);
      const children = byContainer.get(sym.name);
      if (children) node.children = children.map(toSymbol);
      roots.push(node);
    }

    // A member whose container is not itself a top-level symbol would otherwise
    // vanish from the outline entirely. Surfacing it at the root is wrong-looking
    // but visible; dropping it makes the outline quietly incomplete.
    const rootNames = new Set(cached.symbols.filter((s) => s.container.length === 0).map((s) => s.name));
    for (const sym of cached.symbols) {
      if (sym.container.length > 0 && !rootNames.has(sym.container)) roots.push(toSymbol(sym));
    }

    return roots;
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
  field: CompletionItemKind.Field,
  actor: CompletionItemKind.Class,
};

/** A completion item carrying the payload's signature and doc comment. */
function toCompletionItem(sym: SaffronSymbol): CompletionItem {
  const item: CompletionItem = {
    label: sym.name,
    kind: completionKinds[sym.kind] ?? CompletionItemKind.Variable,
  };
  if (sym.detail.length > 0) item.detail = sym.detail;
  if (sym.doc.length > 0) item.documentation = { kind: MarkupKind.Markdown, value: sym.doc };
  return item;
}

/**
 * The directory of the nearest `pantry.toml` at or above `file`, or null.
 *
 * This is the package boundary `internal` is scoped to. Per
 * docs/design/access-modifiers.md: Saffron already calls a *file* a module, so
 * `internal` is deliberately **package**-scoped — the pantry.toml that owns the
 * file — and not directory-scoped. Using the directory would be a different rule
 * that happens to agree in flat layouts and disagree in nested ones.
 */
function packageRootOf(file: string): string | null {
  let dir = path.dirname(path.resolve(file));
  for (;;) {
    if (fs.existsSync(path.join(dir, "pantry.toml"))) return dir;
    const parent = path.dirname(dir);
    if (parent === dir) return null;
    dir = parent;
  }
}

/**
 * Whether a symbol declared in `declFile` may be named from `fromFile`.
 *
 * Only the file-crossing half of the rule is enforced, and deliberately so:
 * this filters *module-member* completion, which is cross-file by construction.
 *
 * - `private` at top level means file-scoped, so a name from another file is out.
 *   (For a class *member* `private` is narrower still — the class body — but a
 *   member is never offered through a module alias, so that case does not arise
 *   here.)
 * - `internal` means package-scoped; see packageRootOf. A file with no
 *   pantry.toml above it has no package, and rather than spell that unknown as a
 *   concrete answer the symbol is treated as accessible — the design leaves this
 *   case open, and over-offering in a completion list is recoverable where
 *   hiding a name the user is entitled to reads as the symbol not existing.
 * - `protected` — visible in the class and its subclasses — needs the
 *   inheritance graph, which the payload does not carry. Treated as accessible
 *   for the same reason: the checker still rejects a real violation.
 */
function isAccessibleFrom(sym: SaffronSymbol, declFile: string, fromFile: string): boolean {
  if (sym.visibility === "public" || sym.visibility === "protected") return true;
  if (path.resolve(declFile) === path.resolve(fromFile)) return true;
  if (sym.visibility === "internal") {
    const declPkg = packageRootOf(declFile);
    const fromPkg = packageRootOf(fromFile);
    if (declPkg === null || fromPkg === null) return true;
    return declPkg === fromPkg;
  }
  return false; // private, in another file
}

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
          const fromFile = decodeURIComponent(params.textDocument.uri.replace("file://", ""));
          // Top-level declarations only — a class's own methods are reached
          // through an instance, not through the module alias — and only those
          // this file is allowed to name.
          return targetCheck.symbols
            .filter((s) => s.container.length === 0)
            .filter(
              (s) =>
                s.kind === "function" ||
                s.kind === "class" ||
                s.kind === "actor" ||
                s.kind === "enum" ||
                s.kind === "interface" ||
                s.kind === "variable",
            )
            .filter((s) => isAccessibleFrom(s, targetFile, fromFile))
            .map(toCompletionItem);
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
        // Members are not in scope as bare names — `radius` alone does not
        // resolve, `this.radius` does — so offering them here suggests code that
        // will not compile. Member completion after `.` is the branch above.
        if (s.container.length > 0) continue;
        if (seen.has(s.name)) continue;
        seen.add(s.name);
        items.push(toCompletionItem(s));
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

// --- References & highlight --------------------------------------------------
//
// Both are name-based, and that is a real limitation rather than a shortcut.
// The compiler resolves *variable* references (resolve.sf produces
// Ref(kind, name, slot)) but leaves member and method accesses unresolved, and
// the --json payload carries declarations only — no reference list. So two
// distinct `count`s, a field on one class and a local in another function, are
// indistinguishable here.
//
// Reporting them together is the honest failure: a superset the user can see
// and dismiss. The alternative — guessing which occurrences belong to the
// binding under the cursor — would silently *omit* real references, and a
// find-references that quietly misses call sites is worse than one that shows
// a few extra. Rename inherits the same caveat and handles it differently, in
// prepareRename below.

/** The occurrence under a cursor position, or null when the cursor is not on code. */
function occurrenceAt(doc: TextDocument, line: number, character: number): Occurrence | null {
  const text = doc.getText();
  const offset = doc.offsetAt({ line, character });
  for (const occ of scanIdentifiers(text)) {
    // Inclusive of the end so a cursor just past the last character — where it
    // sits after typing the name — still counts as being on it.
    if (offset >= occ.offset && offset <= occ.offset + occ.length) return occ;
  }
  return null;
}

connection.onReferences(async (params: ReferenceParams): Promise<Location[]> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];

  const occ = occurrenceAt(doc, params.position.line, params.position.character);
  if (!occ) return [];

  const text = doc.getText();
  const includeDecl = params.context?.includeDeclaration ?? true;

  let cached = fileCache.get(params.textDocument.uri);
  if (!cached) cached = (await runCheck(params.textDocument.uri, text)) ?? undefined;
  const declOffsets = new Set(
    (cached?.symbols ?? []).filter((s) => s.name === occ.name).map((s) => s.offset),
  );

  return occurrencesOf(text, occ.name)
    .filter((o) => includeDecl || !declOffsets.has(o.offset))
    .map((o) => ({
      uri: params.textDocument.uri,
      range: { start: doc.positionAt(o.offset), end: doc.positionAt(o.offset + o.length) },
    }));
});

connection.onDocumentHighlight(
  async (params: DocumentHighlightParams): Promise<DocumentHighlight[]> => {
    const doc = documents.get(params.textDocument.uri);
    if (!doc) return [];

    const occ = occurrenceAt(doc, params.position.line, params.position.character);
    if (!occ) return [];

    const text = doc.getText();
    let cached = fileCache.get(params.textDocument.uri);
    if (!cached) cached = (await runCheck(params.textDocument.uri, text)) ?? undefined;
    const declOffsets = new Set(
      (cached?.symbols ?? []).filter((s) => s.name === occ.name).map((s) => s.offset),
    );

    return occurrencesOf(text, occ.name).map((o) => ({
      range: { start: doc.positionAt(o.offset), end: doc.positionAt(o.offset + o.length) },
      // Write vs Text, not Read: the declaration is the only occurrence we can
      // identify with certainty, because the compiler gave us its offset. Marking
      // an assignment as a write would mean parsing `=` out of the buffer, and a
      // wrong guess miscolours the very thing highlight exists to clarify.
      kind: declOffsets.has(o.offset) ? DocumentHighlightKind.Write : DocumentHighlightKind.Text,
    }));
  },
);

// --- Rename ------------------------------------------------------------------
//
// Rename is name-based like references, but the consequence differs in kind: a
// wrong reference listing is a UI annoyance, a wrong rename is a silent code
// edit. So rename is restricted to names the compiler actually declared in this
// file — prepareRename rejects anything else — and it is single-file.
//
// A name declared in this file may still be *exported* and referenced from
// another, and this rename will not follow it there. Making it cross-file would
// mean compiling every dependent file to find the references, and doing that
// against a name-based matcher would spray edits across the workspace on a name
// collision. The narrow version is the one that cannot corrupt a file the user
// is not looking at.

connection.onPrepareRename(async (params: PrepareRenameParams) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;

  const occ = occurrenceAt(doc, params.position.line, params.position.character);
  if (!occ) return null;

  let cached = fileCache.get(params.textDocument.uri);
  if (!cached) cached = (await runCheck(params.textDocument.uri, doc.getText())) ?? undefined;
  if (!cached) {
    // No payload means the file does not currently compile. Refusing is right:
    // the declaration set is unknown, so there is no way to tell a local
    // declaration from an imported name, and renaming the latter edits this file
    // while leaving its definition alone.
    return new ResponseError(
      ErrorCodes.InvalidRequest,
      "Cannot rename: the file does not compile, so its declarations are unknown.",
    );
  }

  const declared = cached.symbols.some((s) => s.name === occ.name);
  if (!declared) {
    return new ResponseError(
      ErrorCodes.InvalidRequest,
      `Cannot rename '${occ.name}': it is not declared in this file.`,
    );
  }

  return {
    range: { start: doc.positionAt(occ.offset), end: doc.positionAt(occ.offset + occ.length) },
    placeholder: occ.name,
  };
});

connection.onRenameRequest(async (params: RenameParams): Promise<WorkspaceEdit | null> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;

  const occ = occurrenceAt(doc, params.position.line, params.position.character);
  if (!occ) return null;

  // The new name must be a legal identifier. Without this check a stray
  // paste — a space, a dotted path — is applied verbatim and produces a file
  // that no longer parses, with the original name gone from every site.
  if (!IDENT_START.test(params.newName[0] ?? "") || ![...params.newName].every((ch) => IDENT_PART.test(ch))) {
    return new ResponseError(
      ErrorCodes.InvalidRequest,
      `'${params.newName}' is not a valid Saffron identifier.`,
    ) as unknown as null;
  }

  // Re-check the declaration guard. prepareRename is advisory — a client may
  // skip it entirely — so the destructive half enforces its own precondition
  // rather than trusting that the advisory half ran.
  let cached = fileCache.get(params.textDocument.uri);
  if (!cached) cached = (await runCheck(params.textDocument.uri, doc.getText())) ?? undefined;
  if (!cached || !cached.symbols.some((s) => s.name === occ.name)) return null;

  const text = doc.getText();
  const edits = occurrencesOf(text, occ.name).map((o) =>
    TextEdit.replace(
      { start: doc.positionAt(o.offset), end: doc.positionAt(o.offset + o.length) },
      params.newName,
    ),
  );
  if (edits.length === 0) return null;

  return { changes: { [params.textDocument.uri]: edits } };
});

// --- Folding -----------------------------------------------------------------

connection.onFoldingRanges(async (params: FoldingRangeParams): Promise<FoldingRange[]> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];

  let cached = fileCache.get(params.textDocument.uri);
  if (!cached) cached = (await runCheck(params.textDocument.uri, doc.getText())) ?? undefined;

  const ranges: FoldingRange[] = [];

  // Declaration bodies, from the payload's full extent. Only symbols that
  // actually carry one: a folding range invented from a name span folds a
  // single line and reads as a broken control.
  for (const sym of cached?.symbols ?? []) {
    if (sym.start_offset === undefined || sym.end_offset === undefined) continue;
    const start = positionAtByteOffset(doc, sym.start_offset);
    const end = positionAtByteOffset(doc, sym.end_offset);
    // endLine is inclusive in LSP and the extent's last line holds the closing
    // brace, which should stay visible when folded — so fold to the line above
    // it. A one-line declaration then has start === end and is dropped.
    const endLine = end.line - 1;
    if (endLine <= start.line) continue;
    ranges.push({ startLine: start.line, endLine, kind: FoldingRangeKind.Region });
  }

  // Consecutive `///`/`//!` doc-comment runs, which the payload cannot describe:
  // a docstring reaches us as text with its markers stripped, not as a source
  // region. Folding a long doc block is worth the separate scan.
  const lines = doc.getText().split("\n");
  let runStart = -1;
  for (let i = 0; i <= lines.length; i++) {
    const isDoc = i < lines.length && /^\s*\/\/[/!]/.test(lines[i]);
    if (isDoc && runStart === -1) runStart = i;
    else if (!isDoc && runStart !== -1) {
      if (i - 1 > runStart) {
        ranges.push({ startLine: runStart, endLine: i - 1, kind: FoldingRangeKind.Comment });
      }
      runStart = -1;
    }
  }

  return ranges;
});

// --- Signature help ----------------------------------------------------------
//
// The payload's `detail` is a rendered signature — `(a: Int, b: Int): Int` —
// deliberately a display string and not a parseable encoding (see
// diag.sf::symbol_json). Splitting it here to place parameter *ranges* would be
// exactly the reuse that comment warns against: a comma split corrupts on
// `Map<String,Int>`, so `f(m: Map<String,Int>, n: Int)` would report three
// parameters and highlight the wrong one.
//
// So the signature label is shown whole, and active-parameter tracking is done
// by finding the parameter boundaries in the label with a depth-aware scan that
// ignores commas nested inside `<>`, `()` or `[]`.


connection.onSignatureHelp(async (params: SignatureHelpParams): Promise<SignatureHelp | null> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;

  const text = doc.getText();
  const offset = doc.offsetAt(params.position);
  const call = callContextAt(text, offset);
  if (!call) return null;

  let cached = fileCache.get(params.textDocument.uri);
  if (!cached) cached = (await runCheck(params.textDocument.uri, text)) ?? undefined;

  // A callable with a rendered signature. Constructors reach here too: `Circle(1)`
  // finds the class, whose own detail is empty, so prefer its `init`.
  const candidates = (cached?.symbols ?? []).filter(
    (s) => s.name === call.name && s.detail.startsWith("("),
  );
  let sym = candidates[0];
  if (!sym) {
    const cls = (cached?.symbols ?? []).find(
      (s) => s.name === call.name && (s.kind === "class" || s.kind === "actor"),
    );
    if (cls) {
      sym = (cached?.symbols ?? []).find(
        (s) => s.kind === "method" && s.name === "init" && s.container === cls.name,
      )!;
    }
  }
  if (!sym) return null;

  const label = `${sym.name}${sym.detail}`;
  const offsetInLabel = sym.name.length;
  const parameters: ParameterInformation[] = splitParams(sym.detail).map((p) => ({
    // Offset pairs rather than a substring label: a string label is matched by
    // the client against the signature text, and two parameters of the same type
    // would highlight the first one twice.
    label: [offsetInLabel + p.start, offsetInLabel + p.end] as [number, number],
  }));

  const signature: SignatureInformation = { label, parameters };
  if (sym.doc.length > 0) {
    signature.documentation = { kind: MarkupKind.Markdown, value: sym.doc };
  }

  return {
    signatures: [signature],
    activeSignature: 0,
    // Clamped: a call with more arguments than parameters is a real thing to be
    // typing (it is about to be an arity error), and an out-of-range index makes
    // some clients drop the whole response, hiding the signature exactly when it
    // would show the mistake. `undefined` for a no-parameter signature — there is
    // no active parameter to point at, and 0 would highlight a range that is not
    // there.
    activeParameter:
      parameters.length === 0 ? undefined : Math.min(call.argIndex, parameters.length - 1),
  };
});

// --- Semantic tokens ---------------------------------------------------------
//
// What this adds over the TextMate grammar is the distinction the grammar
// cannot make: whether a bare `Foo` is a class, an enum, a variable or a
// parameter. It is driven by the declaration payload, so it classifies
// occurrences of *declared* names and leaves everything else to the grammar.
//
// Same name-based caveat as references, with a smaller blast radius: a
// misclassified token is a wrong colour, not a wrong edit.

const tokenTypeIndex = new Map<string, number>(semanticTokenTypes.map((t, i) => [t, i]));

const symbolKindToTokenType: Record<string, (typeof semanticTokenTypes)[number]> = {
  class: "class",
  // An actor is a class the compiler serializes; there is no `actor` token type
  // in the LSP standard set, and inventing one means themes ignore it.
  actor: "class",
  interface: "interface",
  enum: "enum",
  variant: "enumMember",
  function: "function",
  method: "method",
  field: "property",
  variable: "variable",
  parameter: "parameter",
  module: "class",
};

const visibilityModifierBit: Record<string, number> = {
  private: 1 << semanticTokenModifiers.indexOf("private"),
  protected: 1 << semanticTokenModifiers.indexOf("protected"),
  internal: 1 << semanticTokenModifiers.indexOf("internal"),
};
const declarationBit = 1 << semanticTokenModifiers.indexOf("declaration");

connection.languages.semanticTokens.on(
  async (params: SemanticTokensParams): Promise<SemanticTokens> => {
    const doc = documents.get(params.textDocument.uri);
    if (!doc) return { data: [] };

    let cached = fileCache.get(params.textDocument.uri);
    if (!cached) cached = (await runCheck(params.textDocument.uri, doc.getText())) ?? undefined;
    if (!cached) return { data: [] };

    // Last declaration of a name wins, matching findSymbol's reverse scan, so a
    // shadowed name colours as its most recent meaning rather than its first.
    const byName = new Map<string, SaffronSymbol>();
    for (const s of cached.symbols) byName.set(s.name, s);
    const declOffsets = new Set(cached.symbols.map((s) => s.offset));

    const tokens: { line: number; char: number; length: number; type: number; mods: number }[] = [];
    for (const occ of scanIdentifiers(doc.getText())) {
      const sym = byName.get(occ.name);
      if (!sym) continue;
      const typeName = symbolKindToTokenType[sym.kind];
      if (typeName === undefined) continue;
      const type = tokenTypeIndex.get(typeName);
      if (type === undefined) continue;

      let mods = visibilityModifierBit[sym.visibility] ?? 0;
      if (declOffsets.has(occ.offset)) mods |= declarationBit;

      const pos = doc.positionAt(occ.offset);
      tokens.push({ line: pos.line, char: pos.character, length: occ.length, type, mods });
    }

    // The wire format is delta-encoded: line relative to the previous token,
    // character relative to the previous token *on the same line*. Sorting first
    // is not optional — a single out-of-order token makes every delta after it
    // wrong, which shows up as colours drifting down the file rather than as an
    // error.
    tokens.sort((a, b) => (a.line - b.line) || (a.char - b.char));

    const data: number[] = [];
    let prevLine = 0;
    let prevChar = 0;
    for (const t of tokens) {
      const deltaLine = t.line - prevLine;
      const deltaChar = deltaLine === 0 ? t.char - prevChar : t.char;
      data.push(deltaLine, deltaChar, t.length, t.type, t.mods);
      prevLine = t.line;
      prevChar = t.char;
    }
    return { data };
  },
);

// --- Workspace symbols -------------------------------------------------------

connection.onWorkspaceSymbol(
  async (params: WorkspaceSymbolParams): Promise<WorkspaceSymbol[]> => {
    const query = params.query.toLowerCase();
    const results: WorkspaceSymbol[] = [];

    // Open documents only, not a workspace crawl. Answering across the whole
    // tree would mean compiling every .sf file in it on the first keystroke of
    // the query — for this repo, the compiler's own sources included. The
    // already-compiled files are free, and an empty result for an unopened file
    // is a smaller surprise than a multi-second freeze.
    for (const [uri, check] of fileCache) {
      // fileCache can outlive an open buffer, so this may be undefined; that is
      // the byte-column fallback case and it is exact for an ASCII file.
      const cachedDoc = documents.get(uri);
      for (const sym of check.symbols) {
        // An empty query means "everything", which is what a client sends to
        // populate the picker before the user types.
        if (query.length > 0 && !sym.name.toLowerCase().includes(query)) continue;
        const symbol: WorkspaceSymbol = {
          name: sym.name,
          kind: symbolKinds[sym.kind] ?? SymbolKind.Variable,
          location: { uri, range: symbolRange(sym, cachedDoc) },
        };
        if (sym.container.length > 0) symbol.containerName = sym.container;
        results.push(symbol);
      }
    }
    return results;
  },
);

documents.listen(connection);
connection.listen();
