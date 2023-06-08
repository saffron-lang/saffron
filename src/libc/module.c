#include <printf.h>
#include "module.h"


ObjBuiltinType *moduleType = NULL;

ObjModule *newModule(const char *name, const char *path, bool includeBuiltins) {
//    printf("New module %s\n", name);
    ObjModule *instance = ALLOCATE_OBJ(ObjModule, OBJ_INSTANCE);
    push(OBJ_VAL(instance));
    instance->obj.klass = (ObjClass *) moduleType;
    initTable(&instance->obj.fields);
    instance->result = INTERPRET_OK;
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(OBJ_VAL(copyString(path, (int) strlen(path))));

    instance->path = AS_STRING(peek(0));
    instance->name = AS_STRING(peek(1));
    tableSet(&vm.modules, instance->path, OBJ_VAL(instance));

    pop(); pop();
    if (includeBuiltins) {
        tableAddAll(&vm.builtins, &instance->obj.fields);
    }
    pop();
    return instance;
}

void freeModule(ObjModule *module) {
    FREE(ObjModule, module);
}

void markModule(ObjModule *module) {
    markObject((Obj *) module->name);
    markObject((Obj *) module->path);
}

void printModule(ObjModule *module) {
    printf("<module ");
    if (module->path) {
        printValue(OBJ_VAL(module->path));
    } else {
        printf("uninitialized");
    }
    printf(">");
}

Value moduleCall(int argCount, Value *args) {
    runtimeError("Can't call module.");
    return NIL_VAL;
}

void moduleInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeModule;
    type->markFn = (MarkFn) &markModule;
    type->printFn = (PrintFn) &printModule;
    type->typeCallFn = (TypeCallFn) &moduleCall;
}

ObjBuiltinType *createModuleType() {
    moduleType = newBuiltinType("Module", moduleInit);
    return moduleType;
}

void defineModuleFunction(ObjModule *module, const char *name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&module->obj.fields, AS_STRING(peek(1)), peek(0));
    pop();
    pop();
}

void defineModuleMember(ObjModule *module, const char *name, Value value) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(value);
    tableSet(&module->obj.fields, AS_STRING(peek(1)), peek(0));
    pop();
    pop();
}