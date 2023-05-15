#include "../value.h"
#include "type.h"

#ifndef CRAFTING_INTERPRETERS_ASYNC_H
#define CRAFTING_INTERPRETERS_ASYNC_H

#define AS_GENERATOR(value)       ((ObjGenerator *)AS_OBJ(value))


Value yield(int argCount, Value* args);
Value spawn(int argCount, Value* args);


#define AS_LIST(value) ((ObjList *)AS_OBJ(value))

typedef struct {
    ObjInstance obj;
    ObjCallFrame *frame;
    Obj *stored;
    ValueArray stack;
} ObjGenerator;

ObjGenerator *newGenerator(ObjCallFrame *frame);

Value resume(ObjGenerator *generator, int argCount, Value *args);

ObjBuiltinType *createGeneratorType();

#endif //CRAFTING_INTERPRETERS_ASYNC_H