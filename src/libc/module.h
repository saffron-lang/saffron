#ifndef CRAFTING_INTERPRETERS_MODULE_H
#define CRAFTING_INTERPRETERS_MODULE_H

#include "../object.h"
#include "type.h"
#include "../vm.h"

#define AS_MODULE(value) ((ObjModule *)AS_OBJ(value))
#define IS_MODULE(value)     isObjType(value, OBJ_MODULE)

ObjModule *newModule(const char *name, const char *path, bool includeBuiltins);

void freeModule(ObjModule *module);

void markModule(ObjModule *module);

void printModule(ObjModule *module);

ObjBuiltinType *createModuleType();

void defineModuleFunction(ObjModule *module, const char *name, NativeFn function);
void defineModuleMember(ObjModule *module, const char *name, Value value);

#endif //CRAFTING_INTERPRETERS_MODULE_H
