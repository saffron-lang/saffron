#ifndef CRAFTING_INTERPRETERS_TIME_H
#define CRAFTING_INTERPRETERS_TIME_H

#include "../value.h"
#include "module.h"

#include <time.h>

double getTime();
Value clockNative(int argCount, Value* args);
ObjModule* createTimeModule();
SimpleType *createTimeModuleType();

#endif //CRAFTING_INTERPRETERS_TIME_H
