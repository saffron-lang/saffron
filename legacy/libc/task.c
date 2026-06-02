
#include <printf.h>
#include "task.h"

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

SimpleType *createTaskTypeDef() {
    // Class
    SimpleType *taskTypeDef = newSimpleType();

    // Methods
    FunctorType *getResultType = newFunctorType();
    getResultType->returnType = (Type *) anyType;
    tableSet(
            &taskTypeDef->methods,
            copyString("getResult", 9),
            OBJ_VAL(getResultType)
    );

    FunctorType *isReadyType = newFunctorType();
    isReadyType->returnType = (Type *) boolType;
    tableSet(
            &taskTypeDef->methods,
            copyString("isReady", 7),
            OBJ_VAL(isReadyType)
    );

    return (Type *) taskTypeDef;
}
