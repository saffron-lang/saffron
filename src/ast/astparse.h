#ifndef CRAFTING_INTERPRETERS_ASTPARSE_H
#define CRAFTING_INTERPRETERS_ASTPARSE_H

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

#endif //CRAFTING_INTERPRETERS_ASTPARSE_H
