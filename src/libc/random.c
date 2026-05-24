#include <stdlib.h>
#include <time.h>
#include "random.h"
#include "module.h"

static bool seeded = false;

static void ensureSeeded() {
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }
}

static Value randomFloatFn(int argCount, Value *args) {
    ensureSeeded();
    return NUMBER_VAL((double)rand() / (double)RAND_MAX);
}

static Value randomIntFn(int argCount, Value *args) {
    ensureSeeded();
    int min = (int)AS_NUMBER(args[0]);
    int max = (int)AS_NUMBER(args[1]);
    if (min > max) {
        runtimeError("Random.int: min must be <= max");
        return NIL_VAL;
    }
    int range = max - min + 1;
    int result = min + (rand() % range);
    return NUMBER_VAL((double)result);
}

static Value randomSeedFn(int argCount, Value *args) {
    unsigned int s = (unsigned int)AS_NUMBER(args[0]);
    srand(s);
    seeded = true;
    return NIL_VAL;
}

ObjModule *createRandomModule() {
    ObjModule *module = newModule("Random", "random", false);
    push(OBJ_VAL(module));
    defineModuleFunction(module, "float", randomFloatFn);
    defineModuleFunction(module, "int", randomIntFn);
    defineModuleFunction(module, "seed", randomSeedFn);
    pop();
    return module;
}

SimpleType *createRandomModuleType() {
    SimpleType *mod = newSimpleType();
    createBuiltinFunctorType(mod, "float", (Type *[]) {}, 0, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "int", (Type *[]) {(Type *) numberType, (Type *) numberType}, 2, NULL, 0, (Type *) numberType);
    createBuiltinFunctorType(mod, "seed", (Type *[]) {(Type *) numberType}, 1, NULL, 0, (Type *) nilType);
    return mod;
}

ModuleRegister randomModuleRegister = {
    createRandomModule,
    createRandomModuleType,
    "random",
    "Random",
    false
};
