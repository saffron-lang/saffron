#include "type.h"

ObjNativeMethod *newNativeMethod(NativeMethodFn function) {
    ObjNativeMethod *native = ALLOCATE_OBJ(ObjNativeMethod, OBJ_NATIVE);
    native->function = function;
    return native;
}

void defineBuiltinMethod(ObjBuiltinType *type, const char *name, NativeMethodFn function) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(OBJ_VAL(newNativeMethod(function)));
    tableSet(&type->obj.methods, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

ObjBuiltinType *newBuiltinType(ObjString *name) {
    ObjBuiltinType *klass = ALLOCATE_OBJ(ObjBuiltinType, OBJ_BUILTIN_TYPE);
    klass->obj.name = name;
    initTable(&klass->obj.methods);
    return klass;
}