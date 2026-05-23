#ifndef SAFFRON_ASTPARSE_H
#define SAFFRON_ASTPARSE_H

#include "ast.h"
#include "../diagnostic.h"

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
    bool incomplete;
    bool suppressErrors;
    Node *nodes;
    DiagnosticArray *diagnostics;
} Parser;

StmtArray *parseAST(const char *source);
extern Parser parser;

#endif //SAFFRON_ASTPARSE_H
