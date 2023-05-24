#include "../value.h"
#include "type.h"

#ifndef CRAFTING_INTERPRETERS_ASYNC_H
#define CRAFTING_INTERPRETERS_ASYNC_H

#define AS_GENERATOR(value)       ((ObjTask *)AS_OBJ(value))
#define IS_GENERATOR(value)     isObjType(value, OBJ_GENERATOR)


Value yield(int argCount, Value* args);
Value spawn(int argCount, Value* args);


#define AS_LIST(value) ((ObjList *)AS_OBJ(value))

typedef struct {
    ObjInstance obj;
    ObjCallFrame *frame;
} ObjTask;

ObjTask *newTask(ObjCallFrame *frame);

Value resume(ObjTask *generator, int argCount, Value *args);

ObjBuiltinType *createGeneratorType();

#endif //CRAFTING_INTERPRETERS_ASYNC_H