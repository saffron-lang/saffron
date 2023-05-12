#include <printf.h>
#include "list.h"
#include "../object.h"
#include "../memory.h"


ObjBuiltinType* listType = NULL;

ObjList *newList() {
    ObjList *instance = ALLOCATE_OBJ(ObjList, OBJ_INSTANCE);
    instance->obj.klass = (ObjClass*) listType;
    initTable(&instance->obj.fields);
    initValueArray(&instance->items);
    return instance;
}

void freeList(ObjList *list) {
    freeValueArray(&list->items);
    FREE(ObjList, list);
}

void markList(ObjList *list) {
    markArray(&list->items);
}

void printList(ObjList *list) {
    printf("<list %d>", list->items.count);
}

Value getLength(ObjList *list, int argCount, Value *args) {
    if (argCount > 0) {
        return NIL_VAL;
    } else {
        return NUMBER_VAL(list->items.count);
    }
}

Value listCall(int argCount, Value *args) {
    return OBJ_VAL(newList());
}

void listPush(ObjList *list, Value item) {
    writeValueArray(&list->items, item);
}

ObjBuiltinType *createListType() {
    ObjBuiltinType* type = newBuiltinType(copyString("list", 4));

    type->freeFn = (FreeFn) &freeList;
    type->markFn = (MarkFn) &markList;
    type->printFn = (PrintFn) &printList;
    type->typeCallFn = (TypeCallFn) &listCall;

    defineBuiltinMethod(type, "length", (NativeMethodFn) getLength);

    listType = type;
    return type;
}