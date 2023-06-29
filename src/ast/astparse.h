#ifndef SAFFRON_ASTPARSE_H
#define SAFFRON_ASTPARSE_H

#include "ast.h"

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
    Node *nodes;
} Parser;

StmtArray *parseAST(const char *source);
extern Parser parser;

#endif //SAFFRON_ASTPARSE_H
