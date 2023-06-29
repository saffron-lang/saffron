#ifndef SAFFRON_VALUETABLE_H
#define SAFFRON_VALUETABLE_H


#include "object.h"


typedef struct {
    uint32_t hash;
    Value key;
    Value value;
} MapEntry;

typedef struct {
    int count;
    int capacity;
    MapEntry *entries;
} ValueTable;

uint32_t hash(Value key);

bool valueTableGet(ValueTable *map, Value key, Value *value);

bool valueTableSet(ValueTable *map, Value key, Value item);

bool valueTableDelete(ValueTable *map, uint32_t hash);

void initValueTable(ValueTable *instance);

void freeValueTable(ValueTable *map);

void markValueTable(ValueTable *map);

#endif //SAFFRON_VALUETABLE_H