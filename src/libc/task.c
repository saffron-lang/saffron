
#include <printf.h>
#include "task.h"
#include "../memory.h"

ObjBuiltinType *taskType = NULL;

ObjTask *newTask(ObjCallFrame *task) {
    ObjTask *instance = ALLOCATE_OBJ(ObjTask, OBJ_INSTANCE);
    instance->obj.klass = (ObjClass *) taskType;
    initTable(&instance->obj.fields);
    instance->task = task;
    return instance;
}

void freeTask(ObjTask *task) {
    FREE(ObjTask, task);
}

void markTask(ObjTask *task) {
    markObject(task->task);
}

void printTask(ObjTask *task) {
    printf("<Task %p>", task);
}

Value taskCall(int argCount, Value *args) {
    runtimeError("Can't call task class yet");
    return NIL_VAL;
}

Value getResult(ObjTask *task, int argCount, Value *args) {
    return task->task->result;
}

Value isReady(ObjTask *task, int argCount, Value *args) {
    return BOOL_VAL(!!(task->task->state & FINISHED));
}

void taskInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeTask;
    type->markFn = (MarkFn) &markTask;
    type->printFn = (PrintFn) &printTask;
    type->typeCallFn = (TypeCallFn) &taskCall;
    defineBuiltinMethod(type, "getResult", (NativeMethodFn) getResult);
    defineBuiltinMethod(type, "isReady", (NativeMethodFn) isReady);
}

ObjBuiltinType *createTaskType() {
    taskType = newBuiltinType("Task", taskInit);
    return taskType;
}
