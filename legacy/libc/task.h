#ifndef SAFFRON_TASK_H
#define SAFFRON_TASK_H

#include "../object.h"
#include "../vm.h"
#include "type.h"

typedef struct {
    ObjInstance obj;
    ObjCallFrame *task;
} ObjTask;

ObjTask *newTask(ObjCallFrame *task);

void freeTask(ObjTask *list);

void markTask(ObjTask *list);

void printTask(ObjTask *list);

ObjBuiltinType *createTaskType();

SimpleType *createTaskTypeDef();

#endif //SAFFRON_TASK_H
