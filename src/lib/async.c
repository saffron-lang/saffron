#include <printf.h>
#include "async.h"
#include "../vm.h"
#include "../memory.h"

ObjBuiltinType *generatorType;

ObjTask *newTask(ObjCallFrame *frame) {
    ObjTask *instance = ALLOCATE_OBJ(ObjTask, OBJ_INSTANCE);
    instance->obj.klass = (ObjClass *) generatorType;
    initTable(&instance->obj.fields);
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
    ObjClosure *closure = AS_CLOSURE(args[0]);

    ObjCallFrame *frame = ALLOCATE_OBJ(ObjCallFrame, OBJ_CALL_FRAME);
    writeValueArray(&vm.frames, OBJ_VAL(frame));
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount;
    frame->state = EXECUTING | SPAWNED;
    frame->stored = NIL_VAL;

    initValueArray(&frame->stack);
    printf("EPIC ARGCOUNT, %d\n", argCount);
    for (int i = 0; i < argCount; i++) {
        writeValueArray(&frame->stack, args[i]);
    }

    frame->result = NIL_VAL;
    frame->parent = NULL;
    frame->index = currentFrame->index + 1;

    return NIL_VAL;
}

Value resume(ObjTask *generator, int argCount, Value *args) {

    return generator->frame->stored;
}


void freeGenerator(ObjTask *generator) {
    FREE(ObjTask, generator);
}

void markGenerator(ObjTask *generator) {
    markObject((Obj *) generator->frame);
    markValue(generator->frame->stored);
    markArray(&generator->frame->stack);
}

void printGenerator(ObjTask *generator) {
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