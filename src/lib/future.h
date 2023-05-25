#ifndef CRAFTING_INTERPRETERS_FUTURE_H
#define CRAFTING_INTERPRETERS_FUTURE_H

#include "../object.h"
#include "../vm.h"
#include "type.h"

#define AS_LIST(value) ((ObjList *)AS_OBJ(value))
#define IS_LIST(value)     isObjType(value, OBJ_LIST)

typedef struct {
    ObjInstance obj;
    ObjCallFrame *task;
} ObjFuture;

ObjFuture *newFuture(ObjCallFrame *task);

void freeFuture(ObjFuture *list);

void markFuture(ObjFuture *list);

void printFuture(ObjFuture *list);

ObjBuiltinType *createFutureType();


#endif //CRAFTING_INTERPRETERS_FUTURE_H
