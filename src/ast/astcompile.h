#ifndef CRAFTING_INTERPRETERS_ASTCOMPILE_H
#define CRAFTING_INTERPRETERS_ASTCOMPILE_H

#include "ast.h"
#include "../object.h"

void markCompilerRoots();
ObjFunction *compile(StmtArray *body);

#endif //CRAFTING_INTERPRETERS_ASTCOMPILE_H
