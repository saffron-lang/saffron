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

static Value mathSinFn(int argCount, Value *args) {
    return NUMBER_VAL(sin(AS_NUMBER(args[0])));
}

static Value mathCosFn(int argCount, Value *args) {
    return NUMBER_VAL(cos(AS_NUMBER(args[0])));
}

static Value mathTanFn(int argCount, Value *args) {
    return NUMBER_VAL(tan(AS_NUMBER(args[0])));
}

static Value mathAsinFn(int argCount, Value *args) {
    return NUMBER_VAL(asin(AS_NUMBER(args[0])));
}

static Value mathAcosFn(int argCount, Value *args) {
    return NUMBER_VAL(acos(AS_NUMBER(args[0])));
}

static Value mathAtanFn(int argCount, Value *args) {
    return NUMBER_VAL(atan(AS_NUMBER(args[0])));
}

static Value mathAtan2Fn(int argCount, Value *args) {
    return NUMBER_VAL(atan2(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

static Value mathLogFn(int argCount, Value *args) {
    return NUMBER_VAL(log(AS_NUMBER(args[0])));
}

static Value mathLog2Fn(int argCount, Value *args) {
    return NUMBER_VAL(log2(AS_NUMBER(args[0])));
}

static Value mathLog10Fn(int argCount, Value *args) {
    return NUMBER_VAL(log10(AS_NUMBER(args[0])));
}

static Value mathSignFn(int argCount, Value *args) {
    double x = AS_NUMBER(args[0]);
    return NUMBER_VAL(x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0));
}

static Value mathClampFn(int argCount, Value *args) {
    double x = AS_NUMBER(args[0]);
    double lo = AS_NUMBER(args[1]);
    double hi = AS_NUMBER(args[2]);
    return NUMBER_VAL(x < lo ? lo : (x > hi ? hi : x));
}

ObjModule *createMathModule() {
    ObjModule *module = newModule("Math", "math", false);
    push(OBJ_VAL(module));

    defineModuleMember(module, "pi", NUMBER_VAL(3.14159265358979323846));
    defineModuleMember(module, "e", NUMBER_VAL(2.71828182845904523536));
    defineModuleMember(module, "inf", NUMBER_VAL(INFINITY));
    defineModuleMember(module, "nan", NUMBER_VAL(NAN));

    defineModuleFunction(module, "abs", mathAbsFn);
    defineModuleFunction(module, "floor", mathFloorFn);
    defineModuleFunction(module, "ceil", mathCeilFn);
    defineModuleFunction(module, "round", mathRoundFn);
    defineModuleFunction(module, "min", mathMinFn);
    defineModuleFunction(module, "max", mathMaxFn);
    defineModuleFunction(module, "sqrt", mathSqrtFn);
    defineModuleFunction(module, "pow", mathPowFn);
    defineModuleFunction(module, "random", mathRandomFn);
    defineModuleFunction(module, "sin", mathSinFn);
    defineModuleFunction(module, "cos", mathCosFn);
    defineModuleFunction(module, "tan", mathTanFn);
    defineModuleFunction(module, "asin", mathAsinFn);
    defineModuleFunction(module, "acos", mathAcosFn);
    defineModuleFunction(module, "atan", mathAtanFn);
    defineModuleFunction(module, "atan2", mathAtan2Fn);
    defineModuleFunction(module, "log", mathLogFn);
    defineModuleFunction(module, "log2", mathLog2Fn);
    defineModuleFunction(module, "log10", mathLog10Fn);
    defineModuleFunction(module, "sign", mathSignFn);
    defineModuleFunction(module, "clamp", mathClampFn);

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
    createBuiltinFunctorType(mod, "sin", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "cos", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "tan", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "asin", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "acos", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "atan", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "atan2", (Type *[]) {(Type *) numberType, (Type *) numberType}, 2, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "log", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "log2", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "log10", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "sign", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "clamp", (Type *[]) {(Type *) numberType, (Type *) numberType, (Type *) numberType}, 3, NULL, 0, (Type *) numberType);
    tableSet(&mod->fields, copyString("pi", 2), OBJ_VAL(numberType));
    tableSet(&mod->fields, copyString("e", 1), OBJ_VAL(numberType));
    tableSet(&mod->fields, copyString("inf", 3), OBJ_VAL(numberType));
    tableSet(&mod->fields, copyString("nan", 3), OBJ_VAL(numberType));
    return mod;
}

ModuleRegister mathModuleRegister = {
    createMathModule,
    createMathModuleType,
    "math",
    "Math",
    true
};
