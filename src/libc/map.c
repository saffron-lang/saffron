#include <printf.h>
#include "map.h"
#include "list.h"


ObjBuiltinType *mapType = NULL;

void initMap(ObjMap *instance) {
    instance->obj.klass = (ObjClass *) mapType;
    initValueTable(&instance->values);
    initTable(&instance->obj.fields);
}

ObjMap *newMap() {
    ObjMap *instance = ALLOCATE_OBJ(ObjMap, OBJ_MAP);
    initMap(instance);
    return instance;
}

void printMap(ObjMap *map) {
    printf("{");
    bool skipped = false;
    for (int i = 0; i < map->values.capacity; i++) {
        MapEntry *entry = &map->values.entries[i];
        if (valuesEqual(entry->key, NIL_VAL)) {
            skipped = true;
            continue;
        }
        printValue(entry->key);
        printf(": ");
        printValue(entry->value);
        if (skipped && i != map->values.capacity - 1) {
            printf(", ");
            skipped = false;
        }
    }
    printf("}");
}

SimpleType *createMapTypeDef() {
    // Class
    SimpleType *mapTypeDef = newSimpleType();

    // Methods
    FunctorType *initType = newFunctorType();
    tableSet(
            &mapTypeDef->methods,
            copyString("init", 4),
            OBJ_VAL(initType)
    );

    FunctorType *keysType = newFunctorType();
    keysType->returnType = listTypeDef;
    tableSet(
            &mapTypeDef->methods,
            copyString("keys", 4),
            OBJ_VAL(keysType)
    );

    FunctorType *valuesType = newFunctorType();
    valuesType->returnType = listTypeDef;
    tableSet(
            &mapTypeDef->methods,
            copyString("values", 6),
            OBJ_VAL(valuesType)
    );

    return (Type *) mapTypeDef;
}

//Value mapCall(int argCount, Value *args);


Value getMapItem(ObjMap *map, Value key) {
    if (map->values.count == 0) {
        runtimeError("Accessing empty map. No value at the given key %s", AS_CSTRING(key));
        return NIL_VAL;
    }

    uint32_t keyHash = hash(key);
    uint32_t index = keyHash & (map->values.capacity - 1);
    for (;;) {
        MapEntry *entry = &map->values.entries[index];
        if (valuesEqual(entry->key, NIL_VAL)) {
            if (IS_NIL(entry->value)) {
                runtimeError("No value at the given key %s", AS_CSTRING(key));
                return NIL_VAL;
            }
        } else if (entry->hash == keyHash) {
            return entry->value;
        }
        index = (index + 1) & (map->values.capacity - 1);
    }
}

Value mapKeysBuiltin(ObjMap *map, int argCount) {
    if (argCount > 0) return NIL_VAL;
    ObjList *keys = newList();
    for (int i = 0; i < map->values.capacity; i++) {
        MapEntry *entry = &map->values.entries[i];
        if (!valuesEqual(entry->key, NIL_VAL)) {
            writeValueArray(&keys->items, entry->key);
        }
    }
    return OBJ_VAL(keys);
}

Value mapValuesBuiltin(ObjMap *map, int argCount) {
    if (argCount > 0) return NIL_VAL;
    ObjList *values = newList();
    for (int i = 0; i < map->values.capacity; i++) {
        MapEntry *entry = &map->values.entries[i];
        if (!valuesEqual(entry->key, NIL_VAL)) {
            writeValueArray(&values->items, entry->value);
        }
    }
    return OBJ_VAL(values);
}

void markMap(ValueTable *table) {
    markValueTable(table);
}

void freeMap(ValueTable *table) {
    freeValueTable(table);
}

void mapInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeMap;
    type->markFn = (MarkFn) &markMap;
    type->printFn = (PrintFn) &printMap;
    type->typeCallFn = NULL;
    type->typeDefFn = (GetTypeDefFn) &createMapTypeDef;
    defineBuiltinMethod(type, "keys", (NativeMethodFn) mapKeysBuiltin);
    defineBuiltinMethod(type, "values", (NativeMethodFn) mapValuesBuiltin);
}

ObjBuiltinType *createMapType() {
    mapType = newBuiltinType("Map", mapInit);
    return mapType;
}
