#ifndef CRAFTING_INTERPRETERS_TYPE_H
#define CRAFTING_INTERPRETERS_TYPE_H

#include "../object.h"
#include "../vm.h"

#define AS_NATIVE_METHOD(value)       (((ObjNativeMethod*)AS_OBJ(value))->function)
#define AS_BUILTIN_TYPE(value)       (((ObjBuiltinType *)AS_OBJ(value)))
#define IS_BUILTIN_TYPE(value)        isObjType(value, OBJ_BUILTIN_TYPE)

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
