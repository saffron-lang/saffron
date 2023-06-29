#ifndef SAFFRON_ASTCOMPILE_H
#define SAFFRON_ASTCOMPILE_H

#include "ast.h"
#include "../object.h"

void markCompilerRoots();
ObjFunction *compile(StmtArray *body);

#endif //SAFFRON_ASTCOMPILE_H
