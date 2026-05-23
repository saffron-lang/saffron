#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "value.h"
#include "object.h"
#include "libc/list.h"
#include "libc/map.h"
#include "valuetable.h"
#include "object.h"
#include <math.h>

void initValueArray(ValueArray *array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray *array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values,
                                   oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray *array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

void popValueArray(ValueArray *array, int index) {
    // Move everything to the left 1
    for (int i = index; i < array->count; i++) {
        array->values[i] = array->values[i + 1];
    }
    array->values[array->count - 1] = NIL_VAL;
    array->count -= 1;
}


void printValue(Value value) {
#ifdef NAN_BOXING
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_NUMBER(value)) {
        printf("%g", AS_NUMBER(value));
    } else if (IS_OBJ(value)) {
        printObject(value);
    }
#else
    switch (value.type) {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_NUMBER:
            printf("%g", AS_NUMBER(value));
            break;
        case VAL_OBJ: printObject(value); break;
    }
#endif
}

bool valuesEqual(Value a, Value b) {
#ifdef NAN_BOXING
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return AS_NUMBER(a) == AS_NUMBER(b);
    }
    return a == b;
#else
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:    return true;
        case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_OBJ: {
            if (AS_OBJ(a) == AS_OBJ(b)) return true;
            if (AS_OBJ(a)->type != AS_OBJ(b)->type) return false;
            if (AS_OBJ(a)->type == OBJ_STRING) {
                ObjString *sa = AS_STRING(a), *sb = AS_STRING(b);
                return sa->length == sb->length && memcmp(sa->chars, sb->chars, sa->length) == 0;
            }
            if (AS_OBJ(a)->type == OBJ_LIST) {
                ObjList *la = (ObjList *) AS_OBJ(a), *lb = (ObjList *) AS_OBJ(b);
                if (la->items.count != lb->items.count) return false;
                for (int i = 0; i < la->items.count; i++) {
                    if (!valuesEqual(la->items.values[i], lb->items.values[i])) return false;
                }
                return true;
            }
            if (AS_OBJ(a)->type == OBJ_MAP) {
                ObjMap *ma = (ObjMap *) AS_OBJ(a), *mb = (ObjMap *) AS_OBJ(b);
                if (ma->values.count != mb->values.count) return false;
                for (int i = 0; i < ma->values.capacity; i++) {
                    MapEntry *entry = &ma->values.entries[i];
                    if (IS_NIL(entry->key) && IS_NIL(entry->value)) continue;
                    if (IS_NIL(entry->key)) continue;
                    Value bVal;
                    if (!valueTableGet(&mb->values, entry->key, &bVal)) return false;
                    if (!valuesEqual(entry->value, bVal)) return false;
                }
                return true;
            }
            return false;
        }
        default:         return false; // Unreachable.
    }
#endif
}

double valuesCmp(Value a, Value b) {
    if (a.type != b.type) return NAN;
    switch (a.type) {
        case VAL_BOOL:   return NAN;
        case VAL_NIL:    return NAN;
        case VAL_NUMBER: return AS_NUMBER(a) - AS_NUMBER(b);
        case VAL_OBJ:    return NAN;
        default:         return NAN; // Unreachable.
    }
}
