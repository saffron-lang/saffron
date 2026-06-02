#ifndef SAFFRON_TIME_H
#define SAFFRON_TIME_H

#include "../value.h"
#include "module.h"

#include <time.h>

double getTime();
Value clockNative(int argCount, Value* args);
ObjModule* createTimeModule();
SimpleType *createTimeModuleType();
extern ModuleRegister timeModuleRegister;

#endif //SAFFRON_TIME_H
