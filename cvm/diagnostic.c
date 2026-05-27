#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diagnostic.h"
#include "ast/ast.h"

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

void initSymbolArray(SymbolArray *array) {
    array->count = 0;
    array->capacity = 0;
    array->items = NULL;
}

void writeSymbol(SymbolArray *array, Token *name, SymbolKind kind, const char *typeStr) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = oldCapacity < 8 ? 8 : oldCapacity * 2;
        array->items = realloc(array->items, sizeof(Symbol) * array->capacity);
    }

    Symbol *sym = &array->items[array->count++];
    sym->name = name->start;
    sym->nameLength = name->length;
    sym->defLine = name->line;
    sym->defColumn = name->column;
    sym->defLength = name->length;
    sym->kind = kind;
    sym->typeStr = typeStr;
}

void freeSymbolArray(SymbolArray *array) {
    free(array->items);
    initSymbolArray(array);
}

void collectSymbols(void *stmts, SymbolArray *array) {
    StmtArray *statements = (StmtArray *)stmts;
    for (int i = 0; i < statements->count; i++) {
        Node *node = (Node *)statements->stmts[i];
        if (node->type == NODE_VAR) {
            struct Var *var = (struct Var *)node;
            writeSymbol(array, &var->name, SYM_VARIABLE, NULL);
        } else if (node->type == NODE_FUNCTION) {
            struct Function *fun = (struct Function *)node;
            writeSymbol(array, &fun->name, SYM_FUNCTION, NULL);
            for (int j = 0; j < fun->params.count; j++) {
                writeSymbol(array, &fun->params.parameters[j]->name, SYM_PARAMETER, NULL);
            }
            collectSymbols(&fun->body, array);
        } else if (node->type == NODE_CLASS) {
            struct Class *cls = (struct Class *)node;
            writeSymbol(array, &cls->name, SYM_CLASS, NULL);
            collectSymbols(&cls->body, array);
        } else if (node->type == NODE_BLOCK) {
            struct Block *block = (struct Block *)node;
            collectSymbols(&block->statements, array);
        } else if (node->type == NODE_IMPORT) {
            struct Import *imp = (struct Import *)node;
            writeSymbol(array, &imp->name, SYM_MODULE, NULL);
        } else if (node->type == NODE_ENUM) {
            struct Enum *en = (struct Enum *)node;
            writeSymbol(array, &en->name, SYM_ENUM, NULL);
            collectSymbols(&en->body, array);
        } else if (node->type == NODE_ENUMITEM) {
            struct EnumItem *item = (struct EnumItem *)node;
            writeSymbol(array, &item->name, SYM_ENUM_ITEM, NULL);
        } else if (node->type == NODE_INTERFACE) {
            struct Interface *iface = (struct Interface *)node;
            writeSymbol(array, &iface->name, SYM_INTERFACE, NULL);
            collectSymbols(&iface->body, array);
        } else if (node->type == NODE_METHODSIG) {
            struct MethodSig *sig = (struct MethodSig *)node;
            writeSymbol(array, &sig->name, SYM_METHOD, NULL);
        } else if (node->type == NODE_FORIN) {
            struct ForIn *forin = (struct ForIn *)node;
            writeSymbol(array, &forin->binding, SYM_VARIABLE, NULL);
        }
    }
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

static void printJsonStringN(FILE *out, const char *str, int len) {
    fputc('"', out);
    for (int i = 0; i < len; i++) {
        char c = str[i];
        switch (c) {
            case '"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            default: fputc(c, out); break;
        }
    }
    fputc('"', out);
}

void printDiagnosticsJson(DiagnosticArray *array, const char *filename) {
    printCheckJson(array, NULL, filename);
}

static const char *kindToString(SymbolKind kind) {
    switch (kind) {
        case SYM_VARIABLE: return "variable";
        case SYM_FUNCTION: return "function";
        case SYM_CLASS: return "class";
        case SYM_PARAMETER: return "parameter";
        case SYM_MODULE: return "module";
        case SYM_ENUM: return "enum";
        case SYM_ENUM_ITEM: return "variant";
        case SYM_INTERFACE: return "interface";
        case SYM_METHOD: return "method";
    }
    return "variable";
}

void printCheckJson(DiagnosticArray *diagnostics, SymbolArray *symbols, const char *filename) {
    printf("{\"file\":");
    printJsonString(stdout, filename);
    printf(",\"diagnostics\":[");

    for (int i = 0; i < diagnostics->count; i++) {
        if (i > 0) printf(",");
        Diagnostic *d = &diagnostics->items[i];
        printf("{\"line\":%d,\"column\":%d,\"length\":%d,\"severity\":",
               d->line, d->column, d->length);
        printJsonString(stdout, d->severity == DIAG_ERROR ? "error" : "warning");
        printf(",\"message\":");
        printJsonString(stdout, d->message);
        printf("}");
    }

    printf("],\"symbols\":[");

    if (symbols) {
        for (int i = 0; i < symbols->count; i++) {
            if (i > 0) printf(",");
            Symbol *s = &symbols->items[i];
            printf("{\"name\":");
            printJsonStringN(stdout, s->name, s->nameLength);
            printf(",\"kind\":\"%s\",\"line\":%d,\"column\":%d,\"length\":%d}",
                   kindToString(s->kind), s->defLine, s->defColumn, s->defLength);
        }
    }

    printf("]}\n");
}
