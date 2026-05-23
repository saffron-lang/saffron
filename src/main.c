#include "common.h"
#include "vm.h"
#include "files.h"
#include "ast/ast.h"
#include "ast/astprint.h"
#include "ast/astparse.h"
#include "types.h"
#include "diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void repl() {
    char buffer[8192];
    int bufLen = 0;
    ObjModule *module = interpret(NULL, "<repl>", "<repl>");

    printf("saffron v0.1 REPL\n");
    for (;;) {
        printf(bufLen == 0 ? ">>> " : "... ");

        char line[1024];
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        int lineLen = (int) strlen(line);
        if (bufLen + lineLen >= (int) sizeof(buffer) - 1) {
            fprintf(stderr, "Input too long.\n");
            bufLen = 0;
            continue;
        }
        memcpy(buffer + bufLen, line, lineLen);
        bufLen += lineLen;
        buffer[bufLen] = '\0';

        if (bufLen == 1 && buffer[0] == '\n') {
            bufLen = 0;
            continue;
        }

        // Try parsing silently to detect incomplete input
        parser.suppressErrors = true;
        StmtArray *body = parseAST(buffer);
        parser.suppressErrors = false;

        if (body == NULL && parser.incomplete) {
            continue;
        }

        if (body == NULL) {
            parseAST(buffer); // re-parse to show error
            bufLen = 0;
            continue;
        }

        bufLen = 0;
        InterpretResult result = interpretInModule(body, module);
        if (result == INTERPRET_RUNTIME_ERROR) {
            fprintf(stderr, "Runtime error.\n");
        }
    }
}

static void runFile(const char *path) {
    char *source = readFile(path);
    StmtArray *body = parseAST(source);
    if (body == NULL) {
        free(source);
        exit(65);
    }
    bool typeErrors = evaluateTree(body);
    if (typeErrors) {
        free(source);
        exit(65);
    }
    ObjModule *module = interpret(body, "<script>", path);
    free(source);

    if (module->result == INTERPRET_COMPILE_ERROR) exit(65);
    if (module->result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void parseFile(const char *path) {
    char *source = readFile(path);
    StmtArray *body = parseAST(source);
    printTree(body);
    free(source);

    if (body == NULL) exit(65);
}

static void checkFile(const char *path) {
    char *source = readFile(path);

    DiagnosticArray diagnostics;
    initDiagnosticArray(&diagnostics);

    parser.diagnostics = &diagnostics;
    StmtArray *body = parseAST(source);
    parser.diagnostics = NULL;

    if (body != NULL) {
        setTypeDiagnostics(&diagnostics);
        evaluateTree(body);
        setTypeDiagnostics(NULL);
    }

    printDiagnosticsJson(&diagnostics, path);
    int exitCode = diagnostics.count > 0 ? 65 : 0;
    freeDiagnosticArray(&diagnostics);
    free(source);

    exit(exitCode);
}

int main(int argc, const char *argv[]) {
    initVM();
    if (argc == 1) {
        repl();
    } else if (argc == 2) {
        if (strcmp(argv[1], "--help") == 0) {
            printf("Usage: saffron [--check] [path]\n");
            exit(0);
        }
        runFile(argv[1]);
    } else if (argc == 3 && strcmp(argv[1], "--check") == 0) {
        checkFile(argv[2]);
    } else {
        fprintf(stderr, "Usage: saffron [--check] [path]\n");
        exit(64);
    }

    freeVM();
    return 0;
}