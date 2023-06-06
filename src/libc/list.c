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
    if (index > list->items.count-1 || index < 0) {
        runtimeError("Index out of bounds");
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

void listPushBuiltin(ObjList *list, int argCount, Value *args) {
    if (argCount != 1) {
        return;
    }
    writeValueArray(&list->items, args[0]);
}

Value listPopBuiltin(ObjList *list, int argCount) {
    if (argCount > 0 || list->items.count == 0) {
        return NIL_VAL;
    }
    Value poppedValue = list->items.values[0];
    popValueArray(&list->items, 0);
    return poppedValue;
}

void listReverseBuiltin(ObjList *list, int argCount, Value *args) {
    if (argCount > 0) {
        return;
    }
    for (int i = 0; i < list->items.count / 2; i++) {
        Value tmp = list->items.values[i];
        list->items.values[i] = list->items.values[list->items.count-i-1];
        list->items.values[list->items.count-i-1] = tmp;
    }
}

Value listCopyBuiltin(ObjList *list, int argCount, Value *args) {
    if (argCount > 0) {
        return NIL_VAL;
    }
    ObjList *copy = newList();
    for (int i = 0; i < list->items.count; i++) {
        writeValueArray(&copy->items, list->items.values[i]);
    }
    return OBJ_VAL(copy);
}

Type* listTypeDef() {
    // Class
    SimpleType *listTypeDef = newSimpleType();

    // Methods
    SimpleType *lengthType = newSimpleType();
    lengthType->returnType = numberType;
    tableSet(
            &listTypeDef->methods,
            copyString("length", 6),
            OBJ_VAL(lengthType)
    );

    SimpleType *pushType = newSimpleType();
    writeValueArray(&pushType->arguments, OBJ_VAL(numberType));
    pushType->returnType = nilType;
    tableSet(
            &listTypeDef->methods,
            copyString("push", 4),
            OBJ_VAL(pushType)
    );

    SimpleType *popType = newSimpleType();
    popType->returnType = newSimpleType();
    tableSet(
            &listTypeDef->methods,
            copyString("pop", 3),
            OBJ_VAL(popType)
    );

    SimpleType *reverseType = newSimpleType();
    reverseType->returnType = nilType;
    tableSet(
            &listTypeDef->methods,
            copyString("reverse", 7),
            OBJ_VAL(reverseType)
    );

    SimpleType *copyType = newSimpleType();
    copyType->returnType = listType;
    tableSet(
            &listTypeDef->methods,
            copyString("copy", 4),
            OBJ_VAL(copyType)
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
    defineBuiltinMethod(type, "push", (NativeMethodFn) listPushBuiltin);
    defineBuiltinMethod(type, "pop", (NativeMethodFn) listPopBuiltin);
    defineBuiltinMethod(type, "reverse", (NativeMethodFn) listReverseBuiltin);
    defineBuiltinMethod(type, "copy", (NativeMethodFn) listCopyBuiltin);
}

ObjBuiltinType *createListType() {
    listType = newBuiltinType("list", listInit);
    return listType;
}