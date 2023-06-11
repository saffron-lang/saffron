#include "time.h"

double getTime() {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    return (double) start.tv_sec + (double) start.tv_nsec / 1e9;
}

Value clockNative(int argCount, Value *args) {
    if (argCount > 0) {
        runtimeError("Too many args, expected 0");
    }

    return NUMBER_VAL(getTime());
}

ObjModule *createTimeModule() {
    ObjModule *module = newModule("time", "time", false);
    push(OBJ_VAL(module));
    defineModuleFunction(module, "clock", clockNative);
    pop();
    return module;
}

SimpleType *createTimeModuleType() {
    SimpleType *timeModule = newSimpleType();
    FunctorType *clockType = newFunctorType();
    clockType->returnType = (Type *) numberType;
    tableSet(&timeModule->fields, copyString("clock", 5), OBJ_VAL(clockType));
    return timeModule;
}