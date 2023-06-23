#include <printf.h>
#include "map.h"

#define MAP_MAX_LOAD 0.75

ObjBuiltinType *mapType = NULL;

ObjMap *newMap() {
    ObjMap *instance = ALLOCATE_OBJ(ObjMap, OBJ_MAP);
    instance->obj.klass = (ObjClass *) mapType;
    instance->count = 0;
    instance->capacity = 0;
    initTable(&instance->obj.fields);
    initValueArray(&instance->entries);
    return instance;
}

void freeMap(ObjMap *map) {
    FREE(ObjMap, map);
}

static MapEntry *findEntry(MapEntry *entries, int capacity, int hash) {
    uint32_t index = hash & (capacity - 1);
    MapEntry *tombstone = NULL;

    for (;;) {
        MapEntry *entry = &entries[index];
        if (valuesEqual(entry->key, NIL_VAL)) {
            if (IS_NIL(entry->value)) {
                return tombstone != NULL ? tombstone : entry;
            } else if (tombstone == NULL) {
                tombstone = entry;
            }
        } else if (entry->hash == hash){
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static void adjustCapacity(ObjMap *map, int capacity) {
    MapEntry *entries = ALLOCATE(MapEntry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NIL_VAL;
        entries[i].value = NIL_VAL;
    }

    map->count = 0;
    for (int i = 0; i < map->capacity; i++) {
        MapEntry *entry = &map->entries[i];
        if (valuesEqual(entry->key, NIL_VAL)) continue;

        MapEntry *dest = findEntry(entries, capacity, entry->hash);
        dest->key = entry->key;
        dest->value = entry->value;
        map->count++;
    }

    FREE_ARRAY(MapEntry, map->entries, map->capacity);
    map->entries = entries;
    map->capacity = capacity;
}

bool mapGet(ObjMap *map, Value *key, Value *value, int hash) {
    if (map->count == 0) return false;

    MapEntry *entry = findEntry(map->entries, map->capacity, hash);
    if (valuesEqual(entry->key, NIL_VAL)) return false;

    *value = entry->value;
    return true;
}

void markMap(ObjMap *map) {
    markArray(&map->entries);
}

void printMap(ObjMap *map) {
    // I don't think this will work right
    printf("{");
    bool skipped = false;
    for (int i = 0; i < map->capacity; i++) {
        MapEntry *entry = &map->entries[i];
        if (valuesEqual(entry->key, NIL_VAL)) {
            skipped = true;
            continue;
        }
        printValue(entry->key);
        printf(": ");
        printValue(entry->value);
        if (skipped && i != map->capacity - 1) {
            printf(", ");
            skipped = false;
        }
    }
    printf("}");
}

SimpleType* createMapTypeDef() {
    // Class
    SimpleType *mapTypeDef = newSimpleType();

    // Methods
    FunctorType *initType = newFunctorType();
    tableSet(
            &mapTypeDef->methods,
            copyString("init", 4),
            OBJ_VAL(initType)
    );

    return (Type *) mapTypeDef;
}

//Value mapCall(int argCount, Value *args);

static int hash(Value key) {
    switch (key.type) {
        case VAL_BOOL: return AS_BOOL(key);
        case VAL_NIL: return 0;
        case VAL_NUMBER: return AS_NUMBER(key);
        case VAL_OBJ: {
            Obj *obj = AS_OBJ(key);
            switch (obj->type) {
                case OBJ_STRING: return AS_STRING(key)->hash;
                case OBJ_ATOM: return AS_STRING(key)->hash;
                case OBJ_FUNCTION:
                case OBJ_NATIVE:
                case OBJ_NATIVE_METHOD:
                case OBJ_CLOSURE:
                case OBJ_CLASS:
                case OBJ_BUILTIN_TYPE:
                case OBJ_INSTANCE:
                case OBJ_LIST:
                case OBJ_MAP:
                case OBJ_BOUND_METHOD:
                case OBJ_CALL_FRAME:
                case OBJ_MODULE: return (int) obj;
            }
        }
    }
    return 1;
}

bool mapSet(ObjMap *map, Value key, Value value) {
    if (map->count + 1 > map->capacity * MAP_MAX_LOAD) {
        int capacity = GROW_CAPACITY(map->capacity);
        adjustCapacity(map, capacity);
    }

    MapEntry *entry = findEntry(map->entries, map->capacity, hash(key));
    bool isNewKey = valuesEqual(entry->key, NIL_VAL);
    if (isNewKey) map->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool mapDelete(ObjMap *map, int hash) {
    if (map->count == 0) return false;

    MapEntry *entry = findEntry(map->entries, map->capacity, hash);
    if (valuesEqual(entry->key, NIL_VAL)) return false;

    entry->key = NIL_VAL;
    entry->value = BOOL_VAL(true);
    return true;
}

void mapInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeMap;
    type->markFn = (MarkFn) &markMap;
    type->printFn = (PrintFn) &printMap;
    type->typeCallFn = NULL;
    type->typeDefFn = (GetTypeDefFn) &createMapTypeDef;
}

ObjBuiltinType *createMapType() {
    mapType = newBuiltinType("Map", mapInit);
    return mapType;
}
