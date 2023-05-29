#ifndef CRAFTING_INTERPRETERS_ASTPRINT_H
#define CRAFTING_INTERPRETERS_ASTPRINT_H

#include "ast.h"

void astUnparse(StmtArray* statements);
void unparseNode(Node* node);

void printTree(StmtArray* statements);
void printNode(Node* node);

#endif //CRAFTING_INTERPRETERS_ASTPRINT_H
