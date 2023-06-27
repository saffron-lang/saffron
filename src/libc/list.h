#ifndef CRAFTING_INTERPRETERS_LIST_H
#define CRAFTING_INTERPRETERS_LIST_H

#include "../object.h"
#include "type.h"

#define AS_LIST(value) ((ObjList *)AS_OBJ(value))
#define IS_LIST(value)     isObjType(value, OBJ_LIST)

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

typedef struct {
    ObjInstance obj;
    ValueArray items;
} ObjList;

ObjList *newList();

void freeList(ObjList *list);

void markList(ObjList *list);

void printList(ObjList *list);

SimpleType* createListTypeDef();

Value listCall(int argCount, Value *args);

void listPush(ObjList *list, Value item);

void listPushBuiltin(ObjList *list, int argCount, Value *args);

Value listPopBuiltin(ObjList *list, int argCount);

void listReverseBuiltin(ObjList *list, int argCount, Value *args);

Value listCopyBuiltin(ObjList *list, int argCount, Value *args);

void listSortBuiltin(ObjList *list, int argCount, Value *args);

Value getLength(ObjList *list, int argCount, Value *args);

Value* getListItem(ObjList *list, int index);

ObjBuiltinType *createListType();


#endif //CRAFTING_INTERPRETERS_LIST_H

