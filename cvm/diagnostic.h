#ifndef SAFFRON_DIAGNOSTIC_H
#define SAFFRON_DIAGNOSTIC_H

#include "scanner.h"
#include <stdbool.h>


typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
} DiagnosticSeverity;

typedef struct {
    int line;
    int column;
    int length;
    DiagnosticSeverity severity;
    const char *message;
} Diagnostic;

typedef struct {
    int count;
    int capacity;
    Diagnostic *items;
} DiagnosticArray;

typedef enum {
    SYM_VARIABLE,
    SYM_FUNCTION,
    SYM_CLASS,
    SYM_PARAMETER,
    SYM_MODULE,
    SYM_ENUM,
    SYM_ENUM_ITEM,
    SYM_INTERFACE,
    SYM_METHOD,
} SymbolKind;

typedef struct {
    const char *name;
    int nameLength;
    int defLine;
    int defColumn;
    int defLength;
    SymbolKind kind;
    const char *typeStr;
} Symbol;

typedef struct {
    int count;
    int capacity;
    Symbol *items;
} SymbolArray;

void initDiagnosticArray(DiagnosticArray *array);
void writeDiagnostic(DiagnosticArray *array, Token *token, const char *message, DiagnosticSeverity severity);
void freeDiagnosticArray(DiagnosticArray *array);
void printDiagnosticsJson(DiagnosticArray *array, const char *filename);

void initSymbolArray(SymbolArray *array);
void writeSymbol(SymbolArray *array, Token *name, SymbolKind kind, const char *typeStr);
void freeSymbolArray(SymbolArray *array);
void collectSymbols(void *statements, SymbolArray *array);
void printCheckJson(DiagnosticArray *diagnostics, SymbolArray *symbols, const char *filename);

#endif
