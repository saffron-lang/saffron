#include <printf.h>
#include "io.h"
#include "module.h"

Value printNative(int argCount, Value *args) {
    for (int i = 0; i < argCount; i++) {
        printValue(args[i]);
    }
    return NIL_VAL;
}

Value printlnNative(int argCount, Value *args) {
    printNative(argCount, args);
    printf("\n");
    return NIL_VAL;
}

ObjModule *createIOModule() {
    ObjModule *module = newModule("IO", "io", false);
    push(OBJ_VAL(module));
    defineModuleFunction(module, "print", printNative);
    defineModuleFunction(module, "println", printlnNative);
    pop();
    return module;
}

SimpleType *createIOModuleType() {
    SimpleType *ioModule = newSimpleType();
    createBuiltinFunctorType(ioModule, "print", (Type *[]) {anyType, anyType}, 2, NULL, 0, nilType);;
    createBuiltinFunctorType(ioModule, "println", (Type *[]) {anyType, anyType}, 2, NULL, 0, nilType);;
    return ioModule;
}

ModuleRegister ioModuleRegister = {
        createIOModule,
        createIOModuleType,
        "io",
        "IO",
        true
};