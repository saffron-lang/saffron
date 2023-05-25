#include <printf.h>
#include "type.h"

ObjNativeMethod *newNativeMethod(NativeMethodFn function) {
    ObjNativeMethod *native = ALLOCATE_OBJ(ObjNativeMethod, OBJ_NATIVE_METHOD);
    native->function = function;
    return native;
}

void defineBuiltinMethod(ObjBuiltinType *type, const char *name, NativeMethodFn function) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(OBJ_VAL(newNativeMethod(function)));
    tableSet(&type->obj.methods, AS_STRING(peek(1)), peek(0));
    pop();
    pop();
}

ObjBuiltinType *newBuiltinType(const char *name, InitFn initFn) {
    push(OBJ_VAL(copyString(name, strlen(name))));
    ObjBuiltinType *klass = ALLOCATE_OBJ(ObjBuiltinType, OBJ_BUILTIN_TYPE);
    klass->obj.name = AS_STRING(peek(0));
    initTable(&klass->obj.methods);
    push(OBJ_VAL(klass));
    initFn(klass);
    pop();
    pop();
    return klass;
}