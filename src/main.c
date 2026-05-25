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

int saffronArgc = 0;
const char **saffronArgv = NULL;

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

static void runFileNoCheck(const char *path) {
    char *source = readFile(path);
    StmtArray *body = parseAST(source);
    if (body == NULL) {
        free(source);
        exit(65);
    }
    ObjModule *module = interpret(body, "<script>", path);
    free(source);

    if (module->result == INTERPRET_COMPILE_ERROR) exit(65);
    if (module->result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void runFile(const char *path) {
    char *source = readFile(path);
    StmtArray *body = parseAST(source);
    if (body == NULL) {
        free(source);
        exit(65);
    }
    // Save module docstring before type checker (which may re-invoke parseAST)
    Token savedModuleDoc = parser.moduleDocstring;
    setTypecheckFile(path);
    bool typeErrors = evaluateTree(body);
    setTypecheckFile(NULL);
    parser.moduleDocstring = savedModuleDoc;
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
    SymbolArray symbols;
    initSymbolArray(&symbols);

    parser.diagnostics = &diagnostics;
    StmtArray *body = parseAST(source);
    parser.diagnostics = NULL;

    if (body != NULL) {
        collectSymbols(body, &symbols);
        setTypeDiagnostics(&diagnostics);
        setTypecheckFile(path);
        evaluateTree(body);
        setTypecheckFile(NULL);
        setTypeDiagnostics(NULL);
    }

    printCheckJson(&diagnostics, &symbols, path);
    int exitCode = diagnostics.count > 0 ? 65 : 0;
    freeDiagnosticArray(&diagnostics);
    freeSymbolArray(&symbols);
    free(source);

    exit(exitCode);
}

int main(int argc, const char *argv[]) {
    saffronArgc = argc;
    saffronArgv = argv;
    initVM();
    if (argc == 1) {
        repl();
    } else if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: saffron [--check|--no-check] [path] [args...]\n");
        exit(0);
    } else if (argc >= 3 && strcmp(argv[1], "--check") == 0) {
        checkFile(argv[2]);
    } else if (argc >= 3 && strcmp(argv[1], "--no-check") == 0) {
        saffronArgc = argc - 1;
        saffronArgv = argv + 1;
        runFileNoCheck(argv[2]);
    } else if (argc >= 2) {
        runFile(argv[1]);
    }

    freeVM();
    return 0;
}