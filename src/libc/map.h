#ifndef CRAFTING_INTERPRETERS_MAP_H
#define CRAFTING_INTERPRETERS_MAP_H

#include "../object.h"
#include "type.h"

#define AS_MAP(value) ((ObjMap *)AS_OBJ(value))
#define IS_MAP(value) isObjType(value, OBJ_MAP)

typedef struct {
    int hash;
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

bool mapGet(ObjMap *map, Value *key, Value *value, int hash);

bool mapSet(ObjMap *map, Value key, Value item);

bool mapDelete(ObjMap *map, int hash);

ObjBuiltinType *createMapType();

#endif //CRAFTING_INTERPRETERS_MAP_H
