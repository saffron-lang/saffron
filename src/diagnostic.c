#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diagnostic.h"
#include "memory.h"

void initDiagnosticArray(DiagnosticArray *array) {
    array->count = 0;
    array->capacity = 0;
    array->items = NULL;
}

void writeDiagnostic(DiagnosticArray *array, Token *token, const char *message, DiagnosticSeverity severity) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = oldCapacity < 8 ? 8 : oldCapacity * 2;
        array->items = realloc(array->items, sizeof(Diagnostic) * array->capacity);
    }

    Diagnostic *diag = &array->items[array->count++];
    diag->line = token->line;
    diag->column = token->column;
    diag->length = token->length;
    diag->severity = severity;
    diag->message = message;
}

void freeDiagnosticArray(DiagnosticArray *array) {
    free(array->items);
    initDiagnosticArray(array);
}

static void printJsonString(FILE *out, const char *str) {
    fputc('"', out);
    for (const char *c = str; *c; c++) {
        switch (*c) {
            case '"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            case '\t': fputs("\\t", out); break;
            case '\r': fputs("\\r", out); break;
            default: fputc(*c, out); break;
        }
    }
    fputc('"', out);
}

void printDiagnosticsJson(DiagnosticArray *array, const char *filename) {
    printf("{\"file\":");
    printJsonString(stdout, filename);
    printf(",\"diagnostics\":[");

    for (int i = 0; i < array->count; i++) {
        if (i > 0) printf(",");
        Diagnostic *d = &array->items[i];
        printf("{\"line\":%d,\"column\":%d,\"length\":%d,\"severity\":",
               d->line, d->column, d->length);
        printJsonString(stdout, d->severity == DIAG_ERROR ? "error" : "warning");
        printf(",\"message\":");
        printJsonString(stdout, d->message);
        printf("}");
    }

    printf("]}\n");
}
