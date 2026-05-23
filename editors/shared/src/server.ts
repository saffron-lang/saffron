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
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { execFile } from "child_process";
import { promisify } from "util";

const execFileAsync = promisify(execFile);

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

let compilerPath = "saffron";

connection.onInitialize((params: InitializeParams): InitializeResult => {
  const settings = params.initializationOptions;
  if (settings?.compilerPath) {
    compilerPath = settings.compilerPath;
  }
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Full,
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

interface CheckOutput {
  file: string;
  diagnostics: SaffronDiagnostic[];
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

async function check(uri: string): Promise<Diagnostic[]> {
  const filePath = decodeURIComponent(uri.replace("file://", ""));

  try {
    const { stdout } = await execFileAsync(compilerPath, ["--check", filePath], {
      timeout: 10000,
    });
    const result: CheckOutput = JSON.parse(stdout);
    return mapDiagnostics(result.diagnostics);
  } catch (err: any) {
    if (err.stdout) {
      try {
        const result: CheckOutput = JSON.parse(err.stdout);
        return mapDiagnostics(result.diagnostics);
      } catch {
        // JSON parse failed
      }
    }
    return [];
  }
}

documents.onDidSave(async (event) => {
  const diagnostics = await check(event.document.uri);
  connection.sendDiagnostics({ uri: event.document.uri, diagnostics });
});

documents.onDidOpen(async (event) => {
  const diagnostics = await check(event.document.uri);
  connection.sendDiagnostics({ uri: event.document.uri, diagnostics });
});

documents.onDidClose((event) => {
  connection.sendDiagnostics({ uri: event.document.uri, diagnostics: [] });
});

documents.listen(connection);
connection.listen();
