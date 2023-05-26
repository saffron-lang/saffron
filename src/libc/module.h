#ifndef CRAFTING_INTERPRETERS_MODULE_H
#define CRAFTING_INTERPRETERS_MODULE_H

#include "../object.h"
#include "type.h"
#include "../vm.h"

#define AS_MODULE(value) ((ObjModule *)AS_OBJ(value))
#define IS_MODULE(value)     isObjType(value, OBJ_MODULE)

ObjModule *newModule(const char *name, const char *path);

void freeModule(ObjModule *module);

void markModule(ObjModule *module);

void printModule(ObjModule *module);

ObjBuiltinType *createModuleType();

#endif //CRAFTING_INTERPRETERS_MODULE_H
