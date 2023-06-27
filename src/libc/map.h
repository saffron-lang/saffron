#ifndef CRAFTING_INTERPRETERS_MAP_H
#define CRAFTING_INTERPRETERS_MAP_H

#include "../object.h"
#include "type.h"

#define AS_MAP(value) ((ObjMap *)AS_OBJ(value))
#define IS_MAP(value) isObjType(value, OBJ_MAP)

typedef struct {
    uint32_t hash;
    Value key;
    Value value;
} MapEntry;

typedef struct {
    ObjInstance  obj;
    int count;
    int capacity;
    MapEntry* entries;
} ObjMap;

ObjMap *newMap();

void freeMap(ObjMap *map);

void markMap(ObjMap *map);

void printMap(ObjMap *map);

SimpleType* createMapTypeDef();

Value mapCall(int argCount, Value *args);

bool mapGet(ObjMap *map, Value *key, Value *value, uint32_t hash);

bool mapSet(ObjMap *map, Value key, Value item);

bool mapDelete(ObjMap *map, uint32_t hash);

Value *getMapItem(ObjMap *map, Value key);

ValueArray mapKeysBuiltin(ObjMap *map, int argCount);

ValueArray mapValuesBuiltin(ObjMap *map, int argCount);

ObjBuiltinType *createMapType();

#endif //CRAFTING_INTERPRETERS_MAP_H
