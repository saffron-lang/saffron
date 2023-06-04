#include "common.h"
#include "vm.h"
#include "files.h"
#include "ast/ast.h"
#include "ast/astprint.h"
#include "ast/astparse.h"
#include "compiler.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>

static void repl() {
    char line[1024];
    for (;;) {
        printf("> ");

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

//        interpret(line, "<repl>", "<repl>");
    }
}

static void runFile(const char *path) {
    char *source = readFile(path);
    StmtArray* body = parseAST(source);
    evaluateTree(body);
//    printTree(body);
//    astUnparse(body);
    ObjModule* module = interpret(body, "<script>", path);
    free(source);

    if (module->result == INTERPRET_COMPILE_ERROR) exit(65);
    if (module->result == INTERPRET_RUNTIME_ERROR) exit(70);
}


static void parseFile(const char *path) {
    char *source = readFile(path);
    StmtArray* body = parseAST(source);
    printTree(body);
    free(source);

    if (body==NULL) exit(65);
}


int main(int argc, const char *argv[]) {
    initVM();
    if (argc == 1) {
        repl();
    } else if (argc == 2) {
        runFile(argv[1]);
//        parseFile(argv[1]);
    } else {
        fprintf(stderr, "Usage: clox [path]\n");
        exit(64);
    }

    freeVM();
    return 0;
}