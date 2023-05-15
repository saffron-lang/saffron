#include <printf.h>
#include "async.h"
#include "../vm.h"
#include "../memory.h"

ObjBuiltinType *generatorType;

ObjGenerator *newGenerator(ObjCallFrame *frame) {
    ObjGenerator *instance = ALLOCATE_OBJ(ObjGenerator, OBJ_INSTANCE);
    instance->obj.klass = (ObjClass *) generatorType;
    initTable(&instance->obj.fields);
    initValueArray(&instance->stack);
    instance->frame = frame;
    return instance;
}


Value yield(int argCount, Value *args) {
    return NIL_VAL;
}

Value await(int argCount, Value *args) {
    ObjCallFrame *frame = AS_CALL_FRAME(vm.frames.values[vm.currentFrame]);
    if (frame->index == 0) {
        runtimeError("Cannot yield from top level.");
        return NIL_VAL;
    }


    return NIL_VAL;
}

Value spawn(int argCount, Value *args) {


    return NIL_VAL;
}

Value resume(ObjGenerator *generator, int argCount, Value *args) {

    return OBJ_VAL(generator->stored);
}


void freeGenerator(ObjGenerator *generator) {
    FREE(ObjGenerator, generator);
}

void markGenerator(ObjGenerator *generator) {
    markObject((Obj *) generator->frame);
    markObject((Obj *) generator->stored);
    markArray(&generator->stack);
}

void printGenerator(ObjGenerator *generator) {
    printf("<generator object %p>", generator);
}

void generatorInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeGenerator;
    type->markFn = (MarkFn) &markGenerator;
    type->printFn = (PrintFn) &printGenerator;
    type->typeCallFn = NULL;
    defineBuiltinMethod(type, "resume", (NativeMethodFn) resume);
}

ObjBuiltinType *createGeneratorType() {
    generatorType = newBuiltinType("generator", generatorInit);
    return generatorType;
}