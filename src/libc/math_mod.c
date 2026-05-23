#include <math.h>
#include <stdlib.h>
#include "math_mod.h"
#include "module.h"

static Value mathAbsFn(int argCount, Value *args) {
    return NUMBER_VAL(fabs(AS_NUMBER(args[0])));
}

static Value mathFloorFn(int argCount, Value *args) {
    return NUMBER_VAL(floor(AS_NUMBER(args[0])));
}

static Value mathCeilFn(int argCount, Value *args) {
    return NUMBER_VAL(ceil(AS_NUMBER(args[0])));
}

static Value mathRoundFn(int argCount, Value *args) {
    return NUMBER_VAL(round(AS_NUMBER(args[0])));
}

static Value mathMinFn(int argCount, Value *args) {
    double a = AS_NUMBER(args[0]), b = AS_NUMBER(args[1]);
    return NUMBER_VAL(a < b ? a : b);
}

static Value mathMaxFn(int argCount, Value *args) {
    double a = AS_NUMBER(args[0]), b = AS_NUMBER(args[1]);
    return NUMBER_VAL(a > b ? a : b);
}

static Value mathSqrtFn(int argCount, Value *args) {
    return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
}

static Value mathPowFn(int argCount, Value *args) {
    return NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

static Value mathRandomFn(int argCount, Value *args) {
    return NUMBER_VAL((double)rand() / (double)RAND_MAX);
}

ObjModule *createMathModule() {
    ObjModule *module = newModule("Math", "math", false);
    push(OBJ_VAL(module));

    defineModuleMember(module, "pi", NUMBER_VAL(3.14159265358979323846));
    defineModuleMember(module, "e", NUMBER_VAL(2.71828182845904523536));

    defineModuleFunction(module, "abs", mathAbsFn);
    defineModuleFunction(module, "floor", mathFloorFn);
    defineModuleFunction(module, "ceil", mathCeilFn);
    defineModuleFunction(module, "round", mathRoundFn);
    defineModuleFunction(module, "min", mathMinFn);
    defineModuleFunction(module, "max", mathMaxFn);
    defineModuleFunction(module, "sqrt", mathSqrtFn);
    defineModuleFunction(module, "pow", mathPowFn);
    defineModuleFunction(module, "random", mathRandomFn);

    pop();
    return module;
}

SimpleType *createMathModuleType() {
    SimpleType *mod = newSimpleType();
    createBuiltinFunctorType(mod, "abs", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "floor", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "ceil", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "round", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "min", (Type *[]) {(Type *) numberType, (Type *) numberType}, 2, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "max", (Type *[]) {(Type *) numberType, (Type *) numberType}, 2, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "sqrt", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "pow", (Type *[]) {(Type *) numberType, (Type *) numberType}, 2, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "random", (Type *[]) {}, 0, NULL, 0, (Type *) numberType);
    return mod;
}

ModuleRegister mathModuleRegister = {
    createMathModule,
    createMathModuleType,
    "math",
    "Math",
    true
};
