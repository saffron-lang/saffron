#ifndef CRAFTING_INTERPRETERS_TYPES_H
#define CRAFTING_INTERPRETERS_TYPES_H

#include "ast/ast.h"

void populateTypes(StmtArray* body);
struct Functor* initFunctor(TypeArray types, Type* returnType);
struct Simple* initSimple(Token name);

#endif //CRAFTING_INTERPRETERS_TYPES_H
