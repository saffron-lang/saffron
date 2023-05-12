#include <printf.h>
#include "list.h"
#include "../object.h"
#include "../memory.h"


ObjBuiltinType *listType = NULL;

ObjList *newList() {
    ObjList *instance = ALLOCATE_OBJ(ObjList, OBJ_INSTANCE);
    instance->obj.klass = (ObjClass *) listType;
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
    if (argCount == 0) {
        return OBJ_VAL(newList());
    } else if (argCount == 1) {
        if (IS_STRING(args[0])) {
            ObjList *list = newList();
            push(OBJ_VAL(list));
            ObjString *str = AS_STRING(args[0]);
            for (int i = 0; i < str->length; i++) {
                listPush(list, OBJ_VAL(copyString(&str->chars[i], 1)));
            }

            return pop();
        } else {
            runtimeError("Unexpected type");
            return NIL_VAL;
        }
    } else {
        runtimeError("Expected 0 or 1 arguments");
        return NIL_VAL;
    }
}

void listPush(ObjList *list, Value item) {
    writeValueArray(&list->items, item);
}

void listInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeList;
    type->markFn = (MarkFn) &markList;
    type->printFn = (PrintFn) &printList;
    type->typeCallFn = (TypeCallFn) &listCall;
    defineBuiltinMethod(type, "length", (NativeMethodFn) getLength);
}

ObjBuiltinType *createListType() {
    listType = newBuiltinType("list", listInit);
    return listType;
}