#ifndef CRAFTING_INTERPRETERS_TYPE_H
#define CRAFTING_INTERPRETERS_TYPE_H

#include "../object.h"
#include "../vm.h"

typedef void (*InitFn)(Obj *object);

typedef Obj *(*NewObjectFn)();

typedef void (*FreeFn)(Obj *object);

typedef void (*MarkFn)(Obj *object);

typedef void (*PrintFn)(Obj *object);

typedef Value (*TypeCallFn)(int argCount, Value *args);

typedef Value (*NativeMethodFn)(Obj *object, int argCount, Value *args);

typedef struct {
    Obj obj;
    NativeMethodFn function;
} ObjNativeMethod;


typedef struct {
    ObjClass obj;
    InitFn initFn;
    FreeFn freeFn;
    MarkFn markFn;
//    NewObjectFn *newObjectFn;
    PrintFn printFn;
    TypeCallFn typeCallFn;
} ObjBuiltinType;

ObjNativeMethod *newNativeMethod(NativeMethodFn function);
ObjBuiltinType *newBuiltinType(ObjString* name);

void defineBuiltinMethod(ObjBuiltinType *type, const char *name, NativeMethodFn function);

#endif //CRAFTING_INTERPRETERS_TYPE_H
