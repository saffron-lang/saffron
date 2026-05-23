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
    initType->returnType = (Type *) mapTypeDef;
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

Value mapCall(int argCount, Value *args) {
    return OBJ_VAL(newMap());
}

Value mapSetBuiltin(ObjMap *map, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("set() expects 2 arguments (key, value)");
        return NIL_VAL;
    }
    valueTableSet(&map->values, args[0], args[1]);
    return args[1];
}

Value mapGetBuiltin(ObjMap *map, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("get() expects 1 argument (key)");
        return NIL_VAL;
    }
    Value result;
    if (valueTableGet(&map->values, args[0], &result)) {
        return result;
    }
    return NIL_VAL;
}

Value mapHasBuiltin(ObjMap *map, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("has() expects 1 argument (key)");
        return BOOL_VAL(false);
    }
    Value result;
    return BOOL_VAL(valueTableGet(&map->values, args[0], &result));
}

static Value mapIterBuiltin(ObjMap *map, int argCount, Value *args) {
    return OBJ_VAL(newMapIterator(map));
}

static Value mapLengthBuiltin(ObjMap *map, int argCount, Value *args) {
    return NUMBER_VAL(map->values.count);
}

void mapInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeMap;
    type->markFn = (MarkFn) &markMap;
    type->printFn = (PrintFn) &printMap;
    type->typeCallFn = (TypeCallFn) &mapCall;
    type->typeDefFn = (GetTypeDefFn) &createMapTypeDef;
    defineBuiltinMethod(type, "keys", (NativeMethodFn) mapKeysBuiltin);
    defineBuiltinMethod(type, "values", (NativeMethodFn) mapValuesBuiltin);
    defineBuiltinMethod(type, "set", (NativeMethodFn) mapSetBuiltin);
    defineBuiltinMethod(type, "get", (NativeMethodFn) mapGetBuiltin);
    defineBuiltinMethod(type, "has", (NativeMethodFn) mapHasBuiltin);
    defineBuiltinMethod(type, "iter", (NativeMethodFn) mapIterBuiltin);
    defineBuiltinMethod(type, "length", (NativeMethodFn) mapLengthBuiltin);
}

ObjBuiltinType *createMapType() {
    mapType = newBuiltinType("Map", mapInit);
    return mapType;
}

// --- MapIterator ---

ObjBuiltinType *mapIteratorType = NULL;

ObjMapIterator *newMapIterator(ObjMap *map) {
    ObjMapIterator *iter = ALLOCATE_OBJ(ObjMapIterator, OBJ_INSTANCE);
    iter->obj.klass = (ObjClass *) mapIteratorType;
    initTable(&iter->obj.fields);
    iter->map = map;
    iter->index = 0;
    return iter;
}

static void freeMapIterator(ObjMapIterator *iter) {
    FREE(ObjMapIterator, iter);
}

static void markMapIterator(ObjMapIterator *iter) {
    markObject((Obj *) iter->map);
}

static void printMapIterator(ObjMapIterator *iter) {
    printf("<MapIterator>");
}

static Value mapIteratorNext(ObjMapIterator *iter, int argCount, Value *args) {
    while (iter->index < iter->map->values.capacity) {
        MapEntry *entry = &iter->map->values.entries[iter->index++];
        if (!valuesEqual(entry->key, NIL_VAL)) {
            ObjList *pair = newList();
            writeValueArray(&pair->items, entry->key);
            writeValueArray(&pair->items, entry->value);
            return OBJ_VAL(pair);
        }
    }
    return NIL_VAL;
}

static Value mapIteratorHasNext(ObjMapIterator *iter, int argCount, Value *args) {
    for (int i = iter->index; i < iter->map->values.capacity; i++) {
        if (!valuesEqual(iter->map->values.entries[i].key, NIL_VAL)) {
            return BOOL_VAL(true);
        }
    }
    return BOOL_VAL(false);
}

static Value mapIteratorIter(ObjMapIterator *iter, int argCount, Value *args) {
    return OBJ_VAL(iter);
}

static void mapIteratorInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeMapIterator;
    type->markFn = (MarkFn) &markMapIterator;
    type->printFn = (PrintFn) &printMapIterator;
    type->typeCallFn = NULL;
    type->typeDefFn = NULL;
    defineBuiltinMethod(type, "next", (NativeMethodFn) mapIteratorNext);
    defineBuiltinMethod(type, "next?", (NativeMethodFn) mapIteratorHasNext);
    defineBuiltinMethod(type, "iter", (NativeMethodFn) mapIteratorIter);
}

ObjBuiltinType *createMapIteratorType() {
    mapIteratorType = newBuiltinType("MapIterator", mapIteratorInit);
    return mapIteratorType;
}
