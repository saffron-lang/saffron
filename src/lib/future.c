
#include <printf.h>
#include "future.h"
#include "../memory.h"

ObjBuiltinType *futureType = NULL;

ObjFuture *newFuture(ObjCallFrame *task) {
    ObjFuture *instance = ALLOCATE_OBJ(ObjFuture, OBJ_INSTANCE);
    instance->obj.klass = (ObjClass *) futureType;
    initTable(&instance->obj.fields);
    instance->task = task;
    return instance;
}

void freeFuture(ObjFuture *future) {
    FREE(ObjFuture, future);
}

void markFuture(ObjFuture *future) {
    markObject(future->task);
}

void printFuture(ObjFuture *future) {
    printf("<future %p>", future);
}

Value futureCall(int argCount, Value *args) {
    runtimeError("Can't call future class yet");
    return NIL_VAL;
}

Value getResult(ObjFuture *future, int argCount, Value *args) {
    return future->task->result;
}

Value isReady(ObjFuture *future, int argCount, Value *args) {
    return BOOL_VAL(!!(future->task->state & FINISHED));
}

void futureInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeFuture;
    type->markFn = (MarkFn) &markFuture;
    type->printFn = (PrintFn) &printFuture;
    type->typeCallFn = (TypeCallFn) &futureCall;
    defineBuiltinMethod(type, "getResult", (NativeMethodFn) getResult);
    defineBuiltinMethod(type, "isReady", (NativeMethodFn) isReady);
}

ObjBuiltinType *createFutureType() {
    futureType = newBuiltinType("future", futureInit);
    return futureType;
}
