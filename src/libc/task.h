#ifndef CRAFTING_INTERPRETERS_TASK_H
#define CRAFTING_INTERPRETERS_TASK_H

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


#endif //CRAFTING_INTERPRETERS_TASK_H
