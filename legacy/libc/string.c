#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "string.h"
#include "type.h"
#include "list.h"
#include "../vm.h"
#include "../memory.h"

Table stringMethods;

static Value stringLength(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    return NUMBER_VAL(str->length);
}

static Value stringSplit(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("split() expects 1 string argument");
        return NIL_VAL;
    }
    ObjString *delim = AS_STRING(args[0]);
    ObjList *list = newList();
    push(OBJ_VAL(list));

    if (delim->length == 0) {
        for (int i = 0; i < str->length; i++) {
            listPush(list, OBJ_VAL(copyString(&str->chars[i], 1)));
        }
    } else {
        const char *start = str->chars;
        const char *end = str->chars + str->length;
        const char *found;
        while ((found = strstr(start, delim->chars)) != NULL) {
            listPush(list, OBJ_VAL(copyString(start, (int)(found - start))));
            start = found + delim->length;
        }
        listPush(list, OBJ_VAL(copyString(start, (int)(end - start))));
    }

    return pop();
}

static Value stringTrim(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    const char *start = str->chars;
    const char *end = str->chars + str->length - 1;
    while (start <= end && isspace((unsigned char)*start)) start++;
    while (end > start && isspace((unsigned char)*end)) end--;
    int len = (int)(end - start + 1);
    if (len <= 0) return OBJ_VAL(copyString("", 0));
    return OBJ_VAL(copyString(start, len));
}

static Value stringContains(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("contains() expects 1 string argument");
        return BOOL_VAL(false);
    }
    ObjString *sub = AS_STRING(args[0]);
    return BOOL_VAL(strstr(str->chars, sub->chars) != NULL);
}

static Value stringStartsWith(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("starts_with() expects 1 string argument");
        return BOOL_VAL(false);
    }
    ObjString *prefix = AS_STRING(args[0]);
    if (prefix->length > str->length) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(str->chars, prefix->chars, prefix->length) == 0);
}

static Value stringEndsWith(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("ends_with() expects 1 string argument");
        return BOOL_VAL(false);
    }
    ObjString *suffix = AS_STRING(args[0]);
    if (suffix->length > str->length) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(str->chars + str->length - suffix->length, suffix->chars, suffix->length) == 0);
}

static Value stringReplace(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("replace() expects 2 string arguments");
        return OBJ_VAL(str);
    }
    ObjString *old = AS_STRING(args[0]);
    ObjString *new_ = AS_STRING(args[1]);

    int count = 0;
    const char *tmp = str->chars;
    while ((tmp = strstr(tmp, old->chars)) != NULL) {
        count++;
        tmp += old->length;
    }

    if (count == 0) return OBJ_VAL(str);

    int newLen = str->length + count * (new_->length - old->length);
    char *buf = ALLOCATE(char, newLen + 1);
    char *dst = buf;
    const char *src = str->chars;
    const char *found;
    while ((found = strstr(src, old->chars)) != NULL) {
        int segLen = (int)(found - src);
        memcpy(dst, src, segLen);
        dst += segLen;
        memcpy(dst, new_->chars, new_->length);
        dst += new_->length;
        src = found + old->length;
    }
    int remaining = (int)(str->chars + str->length - src);
    memcpy(dst, src, remaining);
    dst[remaining] = '\0';

    return OBJ_VAL(takeString(buf, newLen));
}

static Value stringToUpper(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    char *buf = ALLOCATE(char, str->length + 1);
    for (int i = 0; i < str->length; i++) {
        buf[i] = (char) toupper((unsigned char) str->chars[i]);
    }
    buf[str->length] = '\0';
    return OBJ_VAL(takeString(buf, str->length));
}

static Value stringToLower(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    char *buf = ALLOCATE(char, str->length + 1);
    for (int i = 0; i < str->length; i++) {
        buf[i] = (char) tolower((unsigned char) str->chars[i]);
    }
    buf[str->length] = '\0';
    return OBJ_VAL(takeString(buf, str->length));
}

static Value stringSlice(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    if (argCount < 1 || argCount > 2) {
        runtimeError("slice() expects 1 or 2 number arguments");
        return OBJ_VAL(str);
    }
    int start = (int) AS_NUMBER(args[0]);
    int end = (argCount == 2) ? (int) AS_NUMBER(args[1]) : str->length;

    if (start < 0) start += str->length;
    if (end < 0) end += str->length;
    if (start < 0) start = 0;
    if (end > str->length) end = str->length;
    if (start >= end) return OBJ_VAL(copyString("", 0));

    return OBJ_VAL(copyString(str->chars + start, end - start));
}

static Value stringIndexOf(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("index_of() expects 1 string argument");
        return NUMBER_VAL(-1);
    }
    ObjString *sub = AS_STRING(args[0]);
    const char *found = strstr(str->chars, sub->chars);
    if (found == NULL) return NUMBER_VAL(-1);
    return NUMBER_VAL((double)(found - str->chars));
}

static Value stringRepeat(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    if (argCount != 1 || !IS_NUMBER(args[0])) {
        runtimeError("repeat() expects 1 number argument");
        return OBJ_VAL(str);
    }
    int count = (int) AS_NUMBER(args[0]);
    if (count <= 0) return OBJ_VAL(copyString("", 0));
    int newLen = str->length * count;
    char *buf = ALLOCATE(char, newLen + 1);
    for (int i = 0; i < count; i++) {
        memcpy(buf + i * str->length, str->chars, str->length);
    }
    buf[newLen] = '\0';
    return OBJ_VAL(takeString(buf, newLen));
}

static Value stringCharAt(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    if (argCount != 1 || !IS_NUMBER(args[0])) {
        runtimeError("char_at() expects 1 number argument");
        return NIL_VAL;
    }
    int index = (int) AS_NUMBER(args[0]);
    if (index < 0) index += str->length;
    if (index < 0 || index >= str->length) return NIL_VAL;
    return OBJ_VAL(copyString(&str->chars[index], 1));
}

static Value stringToNumber(Obj *obj, int argCount, Value *args) {
    ObjString *str = (ObjString *) obj;
    char *end;
    double num = strtod(str->chars, &end);
    if (end == str->chars) return NIL_VAL;
    return NUMBER_VAL(num);
}

static void defineStringMethod(const char *name, NativeMethodFn fn) {
    ObjNativeMethod *method = newNativeMethod(fn);
    tableSet(&stringMethods, copyString(name, (int)strlen(name)), OBJ_VAL(method));
}

void initStringMethods() {
    initTable(&stringMethods);
    defineStringMethod("length", stringLength);
    defineStringMethod("split", stringSplit);
    defineStringMethod("trim", stringTrim);
    defineStringMethod("contains", stringContains);
    defineStringMethod("starts_with", stringStartsWith);
    defineStringMethod("ends_with", stringEndsWith);
    defineStringMethod("replace", stringReplace);
    defineStringMethod("to_upper", stringToUpper);
    defineStringMethod("to_lower", stringToLower);
    defineStringMethod("slice", stringSlice);
    defineStringMethod("index_of", stringIndexOf);
    defineStringMethod("repeat", stringRepeat);
    defineStringMethod("char_at", stringCharAt);
    defineStringMethod("to_number", stringToNumber);
}

void freeStringMethods() {
    freeTable(&stringMethods);
}
