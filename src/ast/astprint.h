#ifndef SAFFRON_ASTPRINT_H
#define SAFFRON_ASTPRINT_H

#include "ast.h"

void astUnparse(StmtArray* statements);
void unparseNode(Node* node);

void printTree(StmtArray* statements);
void printNode(Node* node);

#endif //SAFFRON_ASTPRINT_H
