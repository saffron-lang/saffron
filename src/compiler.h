#ifndef clox_compiler_h
#define clox_compiler_h

#include "vm.h"
#include "object.h"

ObjFunction* compileOld(const char* source);
void markCompilerRoots();

#endif