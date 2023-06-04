#include <printf.h>
#include "list.h"
#include "../memory.h"


ObjBuiltinType *listType = NULL;

ObjList *newList() {
    ObjList *instance = ALLOCATE_OBJ(ObjList, OBJ_LIST);
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
    printf("[");
    for (int i = 0; i < list->items.count; i++) {
        printValue(list->items.values[i]);
        if (i != list->items.count - 1) {
            printf(", ");
        }
    }
    printf("]");
}

Value getLength(ObjList *list, int argCount, Value *args) {
    if (argCount > 0) {
        return NIL_VAL;
    } else {
        return NUMBER_VAL(list->items.count);
    }
}

Value* getItem(ObjList *list, int index) {
    if (index > list->items.count) {
        return NULL;
    } else {
        return &list->items.values[index];
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

Type* listTypeDef() {
    // Class
    SimpleType* listTypeDef = newSimpleType();

    // Methods
    SimpleType* lengthType = newSimpleType();
    lengthType->returnType = (Type *) numberType;
    tableSet(
            &listTypeDef->methods,
            copyString("length", 6),
            OBJ_VAL(lengthType)
    );

    return (Type *) listTypeDef;
}

void listInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeList;
    type->markFn = (MarkFn) &markList;
    type->printFn = (PrintFn) &printList;
    type->typeCallFn = (TypeCallFn) &listCall;
    type->typeDefFn = (GetTypeDefFn) &listTypeDef;
    defineBuiltinMethod(type, "length", (NativeMethodFn) getLength);
}

ObjBuiltinType *createListType() {
    listType = newBuiltinType("list", listInit);
    return listType;
}