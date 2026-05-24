#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "stringbuilder.h"
#include "../memory.h"
#include "../vm.h"

static ObjBuiltinType *stringBuilderType = NULL;

ObjStringBuilder *newStringBuilder() {
    ObjStringBuilder *sb = ALLOCATE_OBJ(ObjStringBuilder, OBJ_INSTANCE);
    sb->obj.klass = (ObjClass *) stringBuilderType;
    initTable(&sb->obj.fields);
    sb->capacity = 64;
    sb->length = 0;
    sb->chars = malloc(sb->capacity);
    sb->chars[0] = '\0';
    return sb;
}

void freeStringBuilder(ObjStringBuilder *sb) {
    free(sb->chars);
    FREE(ObjStringBuilder, sb);
}

void markStringBuilder(ObjStringBuilder *sb) {
    // No GC-managed references inside
}

static void printStringBuilder(ObjStringBuilder *sb) {
    printf("<StringBuilder len=%d>", sb->length);
}

static void sbGrow(ObjStringBuilder *sb, int needed) {
    while (sb->capacity < sb->length + needed + 1) {
        sb->capacity *= 2;
    }
    sb->chars = realloc(sb->chars, sb->capacity);
}

// --- Methods ---

static Value sbAppend(Obj *self, int argCount, Value *args) {
    ObjStringBuilder *sb = (ObjStringBuilder *)self;
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("StringBuilder.append expects a String argument");
        return NIL_VAL;
    }
    ObjString *str = AS_STRING(args[0]);
    sbGrow(sb, str->length);
    memcpy(sb->chars + sb->length, str->chars, str->length);
    sb->length += str->length;
    sb->chars[sb->length] = '\0';
    return OBJ_VAL(self);
}

static Value sbAppendLine(Obj *self, int argCount, Value *args) {
    ObjStringBuilder *sb = (ObjStringBuilder *)self;
    if (argCount >= 1 && IS_STRING(args[0])) {
        ObjString *str = AS_STRING(args[0]);
        sbGrow(sb, str->length + 1);
        memcpy(sb->chars + sb->length, str->chars, str->length);
        sb->length += str->length;
    } else {
        sbGrow(sb, 1);
    }
    sb->chars[sb->length++] = '\n';
    sb->chars[sb->length] = '\0';
    return OBJ_VAL(self);
}

static Value sbToString(Obj *self, int argCount, Value *args) {
    ObjStringBuilder *sb = (ObjStringBuilder *)self;
    return OBJ_VAL(copyString(sb->chars, sb->length));
}

static Value sbLength(Obj *self, int argCount, Value *args) {
    ObjStringBuilder *sb = (ObjStringBuilder *)self;
    return NUMBER_VAL(sb->length);
}

static Value sbClear(Obj *self, int argCount, Value *args) {
    ObjStringBuilder *sb = (ObjStringBuilder *)self;
    sb->length = 0;
    sb->chars[0] = '\0';
    return OBJ_VAL(self);
}

static Value sbCall(int argCount, Value *args) {
    ObjStringBuilder *sb = newStringBuilder();
    return OBJ_VAL(sb);
}

// --- Type Registration ---

static void stringBuilderInit(ObjBuiltinType *type) {
    type->freeFn = (FreeFn) &freeStringBuilder;
    type->markFn = (MarkFn) &markStringBuilder;
    type->printFn = (PrintFn) &printStringBuilder;
    type->typeCallFn = (TypeCallFn) &sbCall;
    type->typeDefFn = NULL;
    defineBuiltinMethod(type, "append", (NativeMethodFn) sbAppend);
    defineBuiltinMethod(type, "append_line", (NativeMethodFn) sbAppendLine);
    defineBuiltinMethod(type, "to_string", (NativeMethodFn) sbToString);
    defineBuiltinMethod(type, "length", (NativeMethodFn) sbLength);
    defineBuiltinMethod(type, "clear", (NativeMethodFn) sbClear);
}

ObjBuiltinType *createStringBuilderType() {
    stringBuilderType = newBuiltinType("StringBuilder", stringBuilderInit);
    return stringBuilderType;
}
