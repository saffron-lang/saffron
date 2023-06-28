#include <stdio.h>
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
//    freeValueArray(&list->items);
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

Value* getListItem(ObjList *list, int index) {
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

void insertionSort(ObjList *list, int l, int r) {
    for (int i = l + 1; i <= r; i++) {
        Value tmp = list->items.values[i];
        int j = i - 1;
        while (j >= l && valuesCmp(list->items.values[j], tmp) > 0) {
            list->items.values[j+1] = list->items.values[j];
            j--;
        }
        list->items.values[j+1] = tmp;
    }
}

void merge(ObjList *list, int l, int  m, int r) {
    int len1 = m - l + 1;
    int len2 = r - m;

    ObjList *left = newList();
    ObjList *right = newList();
    for (int i = 0; i < len1; i++) {
        writeValueArray(&left->items, list->items.values[l + i]);
    }
    for (int i = 0; i < len2; i++) {
        writeValueArray(&right->items, list->items.values[m + 1 + i]);
    }
    int i = 0, j = 0, k = 0;
    while (i < len1 && j < len2) {
        if (valuesCmp(left->items.values[i], right->items.values[j]) <= 0) {
            list->items.values[k] = left->items.values[i];
            i++;
        } else {
            list->items.values[k] = right->items.values[j];
            j++;
        }
        k++;
    }
    while (i < len1) {
        list->items.values[k] = left->items.values[i];
        k++;
        i++;
    }
    while (j < len2) {
        list->items.values[k] = right->items.values[j];
        k++;
        j++;
    }
}

void timSort(ObjList *list, int n) {
    const int RUN = 32;
    for (int i = 0; i < n; i += RUN) {
        insertionSort(list, i, MIN((i + RUN - 1), (n - 1)));
    }
    for (int size = RUN; size < n; size = 2 * size) {
        for (int l = 0; l < n; l += 2 * size) {
            int m = l + size - 1;
            int r = MIN((l + 2 * size - 1), (n - 1));
            if (m < r) {
                merge(list, l, m, r);
            }
        }
    }
}

void listSortBuiltin(ObjList *list, int argCount, Value *args) {
    if (argCount > 0) {
        return;
    }
    timSort(list, list->items.count);
}

SimpleType* createListTypeDef() {
    // Class
    SimpleType *listTypeDef = newSimpleType();

    // Methods
    FunctorType *lengthType = newFunctorType();
    lengthType->returnType = (Type *) numberType;
    tableSet(
            &listTypeDef->methods,
            copyString("length", 6),
            OBJ_VAL(lengthType)
    );

    FunctorType *pushType = newFunctorType();
    writeValueArray(&pushType->arguments, OBJ_VAL(numberType));
    pushType->returnType = (Type *) nilType;
    tableSet(
            &listTypeDef->methods,
            copyString("push", 4),
            OBJ_VAL(pushType)
    );

    FunctorType *popType = newFunctorType();
    popType->returnType = newSimpleType();
    tableSet(
            &listTypeDef->methods,
            copyString("pop", 3),
            OBJ_VAL(popType)
    );

    FunctorType *reverseType = newFunctorType();
    reverseType->returnType = nilType;
    tableSet(
            &listTypeDef->methods,
            copyString("reverse", 7),
            OBJ_VAL(reverseType)
    );

    FunctorType *copyType = newFunctorType();
    copyType->returnType = listType;
    tableSet(
            &listTypeDef->methods,
            copyString("copy", 4),
            OBJ_VAL(copyType)
    );

    FunctorType *sortType = newFunctorType();
    sortType->returnType = nilType;
    tableSet(
            &listTypeDef->methods,
            copyString("sort", 4),
            OBJ_VAL(sortType)
    );

    FunctorType *initType = newFunctorType();
    sortType->returnType = (Type *) listTypeDef;
    tableSet(
            &listTypeDef->methods,
            copyString("init", 4),
            OBJ_VAL(initType)
    );

    return (Type *) listTypeDef;
}

void listInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeList;
    type->markFn = (MarkFn) &markList;
    type->printFn = (PrintFn) &printList;
    type->typeCallFn = (TypeCallFn) &listCall;
    type->typeDefFn = (GetTypeDefFn) &createListTypeDef;
    defineBuiltinMethod(type, "length", (NativeMethodFn) getLength);
    defineBuiltinMethod(type, "push", (NativeMethodFn) listPushBuiltin);
    defineBuiltinMethod(type, "pop", (NativeMethodFn) listPopBuiltin);
    defineBuiltinMethod(type, "reverse", (NativeMethodFn) listReverseBuiltin);
    defineBuiltinMethod(type, "copy", (NativeMethodFn) listCopyBuiltin);
    defineBuiltinMethod(type, "sort", (NativeMethodFn) listSortBuiltin);
}

ObjBuiltinType *createListType() {
    listType = newBuiltinType("List", listInit);
    return listType;
}