#include <stdio.h>
#include <string.h>
#include "reflect.h"
#include "list.h"
#include "map.h"
#include "../memory.h"

// --- Type checks ---

static Value reflectIsNumber(int argCount, Value *args) {
    if (argCount != 1) return BOOL_VAL(false);
    return BOOL_VAL(IS_NUMBER(args[0]));
}

static Value reflectIsString(int argCount, Value *args) {
    if (argCount != 1) return BOOL_VAL(false);
    return BOOL_VAL(IS_STRING(args[0]));
}

static Value reflectIsBool(int argCount, Value *args) {
    if (argCount != 1) return BOOL_VAL(false);
    return BOOL_VAL(IS_BOOL(args[0]));
}

static Value reflectIsNil(int argCount, Value *args) {
    if (argCount != 1) return BOOL_VAL(false);
    return BOOL_VAL(IS_NIL(args[0]));
}

static Value reflectIsList(int argCount, Value *args) {
    if (argCount != 1) return BOOL_VAL(false);
    return BOOL_VAL(IS_OBJ(args[0]) && AS_OBJ(args[0])->type == OBJ_LIST);
}

static Value reflectIsMap(int argCount, Value *args) {
    if (argCount != 1) return BOOL_VAL(false);
    return BOOL_VAL(IS_OBJ(args[0]) && AS_OBJ(args[0])->type == OBJ_MAP);
}

static Value reflectIsInstance(int argCount, Value *args) {
    if (argCount != 1) return BOOL_VAL(false);
    if (!IS_OBJ(args[0])) return BOOL_VAL(false);
    ObjType t = AS_OBJ(args[0])->type;
    return BOOL_VAL(t == OBJ_INSTANCE);
}

static Value reflectIsClass(int argCount, Value *args) {
    if (argCount != 1) return BOOL_VAL(false);
    return BOOL_VAL(IS_CLASS(args[0]));
}

// --- Casting (identity, but documents intent) ---

static Value reflectAsString(int argCount, Value *args) {
    if (argCount != 1) return NIL_VAL;
    return args[0];
}

static Value reflectAsList(int argCount, Value *args) {
    if (argCount != 1) return NIL_VAL;
    return args[0];
}

static Value reflectAsMap(int argCount, Value *args) {
    if (argCount != 1) return NIL_VAL;
    return args[0];
}

// --- Introspection ---

static Value reflectFields(int argCount, Value *args) {
    if (argCount != 1) return OBJ_VAL(newMap());

    if (IS_INSTANCE(args[0])) {
        ObjInstance *instance = AS_INSTANCE(args[0]);
        ObjMap *result = newMap();
        push(OBJ_VAL(result));

        ObjClass *klass = instance->klass;
        if (klass->isDataClass && klass->fieldMetas.count > 0) {
            for (int i = 0; i < klass->fieldMetas.count; i++) {
                FieldMeta *meta = &klass->fieldMetas.entries[i];
                Value fieldVal;
                if (tableGet(&instance->fields, meta->name, &fieldVal)) {
                    valueTableSet(&result->values, OBJ_VAL(meta->name), fieldVal);
                } else {
                    valueTableSet(&result->values, OBJ_VAL(meta->name), NIL_VAL);
                }
            }
        } else {
            for (int i = 0; i < instance->fields.capacity; i++) {
                Entry *entry = &instance->fields.entries[i];
                if (entry->key != NULL) {
                    valueTableSet(&result->values, OBJ_VAL(entry->key), entry->value);
                }
            }
        }

        return pop();
    }

    return OBJ_VAL(newMap());
}

static Value reflectClassName(int argCount, Value *args) {
    if (argCount != 1) return NIL_VAL;
    if (IS_INSTANCE(args[0])) {
        ObjInstance *instance = AS_INSTANCE(args[0]);
        return OBJ_VAL(instance->klass->name);
    }
    if (IS_CLASS(args[0])) {
        ObjClass *klass = AS_CLASS(args[0]);
        return OBJ_VAL(klass->name);
    }
    return NIL_VAL;
}

static Value reflectNumberToString(int argCount, Value *args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%g", AS_NUMBER(args[0]));
    return OBJ_VAL(copyString(buf, len));
}

static Value reflectTypeOf(int argCount, Value *args) {
    if (argCount != 1) return NIL_VAL;
    Value v = args[0];
    if (IS_NIL(v)) return OBJ_VAL(copyString("nil", 3));
    if (IS_BOOL(v)) return OBJ_VAL(copyString("bool", 4));
    if (IS_NUMBER(v)) return OBJ_VAL(copyString("number", 6));
    if (IS_STRING(v)) return OBJ_VAL(copyString("string", 6));
    if (IS_OBJ(v)) {
        switch (AS_OBJ(v)->type) {
            case OBJ_LIST: return OBJ_VAL(copyString("list", 4));
            case OBJ_MAP: return OBJ_VAL(copyString("map", 3));
            case OBJ_INSTANCE: return OBJ_VAL(copyString("instance", 8));
            case OBJ_CLASS: return OBJ_VAL(copyString("class", 5));
            case OBJ_CLOSURE: return OBJ_VAL(copyString("function", 8));
            case OBJ_NATIVE: return OBJ_VAL(copyString("function", 8));
            default: return OBJ_VAL(copyString("object", 6));
        }
    }
    return OBJ_VAL(copyString("unknown", 7));
}

// --- Module registration ---

ObjModule *createReflectModule() {
    ObjModule *module = newModule("Reflect", "reflect", false);
    push(OBJ_VAL(module));

    defineModuleFunction(module, "is_number", reflectIsNumber);
    defineModuleFunction(module, "is_string", reflectIsString);
    defineModuleFunction(module, "is_bool", reflectIsBool);
    defineModuleFunction(module, "is_nil", reflectIsNil);
    defineModuleFunction(module, "is_list", reflectIsList);
    defineModuleFunction(module, "is_map", reflectIsMap);
    defineModuleFunction(module, "is_instance", reflectIsInstance);
    defineModuleFunction(module, "is_class", reflectIsClass);

    defineModuleFunction(module, "as_string", reflectAsString);
    defineModuleFunction(module, "as_list", reflectAsList);
    defineModuleFunction(module, "as_map", reflectAsMap);

    defineModuleFunction(module, "fields", reflectFields);
    defineModuleFunction(module, "class_name", reflectClassName);
    defineModuleFunction(module, "type_of", reflectTypeOf);
    defineModuleFunction(module, "number_to_string", reflectNumberToString);

    pop();
    return module;
}

SimpleType *createReflectModuleType() {
    SimpleType *mod = newSimpleType();
    createBuiltinFunctorType(mod, "is_number", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(mod, "is_string", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(mod, "is_bool", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(mod, "is_nil", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(mod, "is_list", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(mod, "is_map", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(mod, "is_instance", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(mod, "is_class", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(mod, "as_string", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(mod, "as_list", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) anyType);
    createBuiltinFunctorType(mod, "as_map", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) anyType);
    createBuiltinFunctorType(mod, "fields", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) anyType);
    createBuiltinFunctorType(mod, "class_name", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(mod, "type_of", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(mod, "number_to_string", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) stringType);
    return mod;
}

ModuleRegister reflectModuleRegister = {
    createReflectModule,
    createReflectModuleType,
    "reflect",
    "Reflect",
    true
};
