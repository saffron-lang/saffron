#include "../value.h"
#include "type.h"
#include "module.h"

#ifndef SAFFRON_ASYNC_H
#define SAFFRON_ASYNC_H

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

Value spawnNative(int argCount, Value* args);
//Value sleepNative(int argCount, Value* args);

typedef struct {
    ValueArray sleepers;
    ValueArray sleeper_times;
} AsyncHandler;

extern AsyncHandler asyncHandler;

void initAsyncHandler();
void freeAsyncHandler();
void markAsyncRoots();
void handle_yield_value(Value value);
int getTasks();

ModuleRegister taskModuleRegister;

#endif //SAFFRON_ASYNC_H