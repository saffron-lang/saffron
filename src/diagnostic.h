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

void initDiagnosticArray(DiagnosticArray *array);
void writeDiagnostic(DiagnosticArray *array, Token *token, const char *message, DiagnosticSeverity severity);
void freeDiagnosticArray(DiagnosticArray *array);
void printDiagnosticsJson(DiagnosticArray *array, const char *filename);

#endif
