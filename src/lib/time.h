#ifndef CRAFTING_INTERPRETERS_TIME_H
#define CRAFTING_INTERPRETERS_TIME_H

#include "../value.h"

#include <time.h>

double getTime();
Value clockNative(int argCount, Value* args);

#endif //CRAFTING_INTERPRETERS_TIME_H
