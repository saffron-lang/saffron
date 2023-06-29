#ifndef CRAFTING_INTERPRETERS_MAP_H
#define CRAFTING_INTERPRETERS_MAP_H

#include "type.h"
#include "../valuetable.h"

#define AS_MAP(value) ((ObjMap *)AS_OBJ(value))
#define IS_MAP(value) isObjType(value, OBJ_MAP)


typedef struct {
    ObjInstance obj;
    ValueTable values;
} ObjMap;

ObjMap *newMap();

void initMap(ObjMap *instance);

void printMap(ObjMap *map);

SimpleType *createMapTypeDef();

Value mapCall(int argCount, Value *args);

Value getMapItem(ObjMap *map, Value key);

Value mapKeysBuiltin(ObjMap *map, int argCount);

Value mapValuesBuiltin(ObjMap *map, int argCount);

ObjBuiltinType *createMapType();

#endif //CRAFTING_INTERPRETERS_MAP_H
