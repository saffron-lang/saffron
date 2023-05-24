#include "../value.h"
#include "type.h"

#ifndef CRAFTING_INTERPRETERS_ASYNC_H
#define CRAFTING_INTERPRETERS_ASYNC_H

typedef enum {
    SLEEP = 1,
    WAIT_IO_READ = 2,
    WAIT_IO_WRITE = 4,
} YieldType;

typedef struct {
    ObjCallFrame *task;
    double time;
} Sleeper;

#define AS_SLEEPER(value) ((Sleeper*)AS_OBJ(value))

Value spawn(int argCount, Value* args);
Value sleep(int argCount, Value* args);

typedef struct {
    ValueArray sleepers;
} AsyncHandler;

extern AsyncHandler asyncHandler;

void initAsyncHandler();
void freeAsyncHandler();
void markAsyncRoots();
void handle_yield_value(Value value);

#endif //CRAFTING_INTERPRETERS_ASYNC_H