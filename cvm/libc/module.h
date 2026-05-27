#ifndef SAFFRON_MODULE_H
#define SAFFRON_MODULE_H

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

typedef ObjModule *(*CreateModuleFn)();

typedef SimpleType *(*CreateModuleTypeFn)();

FunctorType *createBuiltinFunctorType(
        SimpleType *moduleType,
        const char *name,
        Type **arguments,
        int argumentCount,
        struct TypeDeclaration **genericArgs,
        int genericCount,
        Type *returnType
);

typedef struct ModuleRegister {
    CreateModuleFn createModuleFn;
    CreateModuleTypeFn createModuleTypeFn;
    const char* path;
    const char* name;
    bool builtin;
} ModuleRegister;

#endif //SAFFRON_MODULE_H
