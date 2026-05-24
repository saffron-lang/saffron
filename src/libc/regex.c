#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include "regex.h"
#include "list.h"
#include "map.h"
#include "../memory.h"
#include "../vm.h"

static Value regexMatch(int argCount, Value *args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("Regex.match expects (pattern: String, text: String)");
        return NIL_VAL;
    }
    ObjString *pattern = AS_STRING(args[0]);
    ObjString *text = AS_STRING(args[1]);

    regex_t reg;
    if (regcomp(&reg, pattern->chars, REG_EXTENDED) != 0) {
        runtimeError("Invalid regex pattern: %s", pattern->chars);
        return NIL_VAL;
    }

    regmatch_t match;
    bool matched = regexec(&reg, text->chars, 1, &match, 0) == 0;
    regfree(&reg);
    return BOOL_VAL(matched);
}

static Value regexFind(int argCount, Value *args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("Regex.find expects (pattern: String, text: String)");
        return NIL_VAL;
    }
    ObjString *pattern = AS_STRING(args[0]);
    ObjString *text = AS_STRING(args[1]);

    regex_t reg;
    if (regcomp(&reg, pattern->chars, REG_EXTENDED) != 0) {
        runtimeError("Invalid regex pattern: %s", pattern->chars);
        return NIL_VAL;
    }

    regmatch_t pmatch[10];
    if (regexec(&reg, text->chars, 10, pmatch, 0) != 0) {
        regfree(&reg);
        return NIL_VAL;
    }

    ObjMap *result = newMap();
    push(OBJ_VAL(result));

    int start = (int)pmatch[0].rm_so;
    int end = (int)pmatch[0].rm_eo;
    ObjString *matched = copyString(text->chars + start, end - start);

    valueTableSet(&result->values,
        OBJ_VAL(copyString("match", 5)), OBJ_VAL(matched));
    valueTableSet(&result->values,
        OBJ_VAL(copyString("start", 5)), NUMBER_VAL(start));
    valueTableSet(&result->values,
        OBJ_VAL(copyString("end", 3)), NUMBER_VAL(end));

    ObjList *groups = newList();
    push(OBJ_VAL(groups));
    for (int i = 1; i < 10 && pmatch[i].rm_so != -1; i++) {
        int gs = (int)pmatch[i].rm_so;
        int ge = (int)pmatch[i].rm_eo;
        writeValueArray(&groups->items, OBJ_VAL(copyString(text->chars + gs, ge - gs)));
    }
    valueTableSet(&result->values,
        OBJ_VAL(copyString("groups", 6)), OBJ_VAL(groups));
    pop(); // groups
    pop(); // result

    regfree(&reg);
    return OBJ_VAL(result);
}

static Value regexFindAll(int argCount, Value *args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("Regex.find_all expects (pattern: String, text: String)");
        return NIL_VAL;
    }
    ObjString *pattern = AS_STRING(args[0]);
    ObjString *text = AS_STRING(args[1]);

    regex_t reg;
    if (regcomp(&reg, pattern->chars, REG_EXTENDED) != 0) {
        runtimeError("Invalid regex pattern: %s", pattern->chars);
        return NIL_VAL;
    }

    ObjList *results = newList();
    push(OBJ_VAL(results));

    const char *cursor = text->chars;
    regmatch_t pmatch;
    while (regexec(&reg, cursor, 1, &pmatch, 0) == 0) {
        int start = (int)pmatch.rm_so;
        int end = (int)pmatch.rm_eo;
        if (start == end) { cursor++; continue; }
        ObjString *matched = copyString(cursor + start, end - start);
        writeValueArray(&results->items, OBJ_VAL(matched));
        cursor += end;
    }

    pop();
    regfree(&reg);
    return OBJ_VAL(results);
}

static Value regexReplace(int argCount, Value *args) {
    if (argCount != 3 || !IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
        runtimeError("Regex.replace expects (pattern: String, text: String, replacement: String)");
        return NIL_VAL;
    }
    ObjString *pattern = AS_STRING(args[0]);
    ObjString *text = AS_STRING(args[1]);
    ObjString *replacement = AS_STRING(args[2]);

    regex_t reg;
    if (regcomp(&reg, pattern->chars, REG_EXTENDED) != 0) {
        runtimeError("Invalid regex pattern: %s", pattern->chars);
        return NIL_VAL;
    }

    char buf[8192];
    int bufLen = 0;
    const char *cursor = text->chars;
    regmatch_t pmatch;

    while (regexec(&reg, cursor, 1, &pmatch, 0) == 0) {
        int start = (int)pmatch.rm_so;
        int end = (int)pmatch.rm_eo;
        if (start == end) { if (*cursor) buf[bufLen++] = *cursor++; continue; }

        memcpy(buf + bufLen, cursor, start);
        bufLen += start;
        memcpy(buf + bufLen, replacement->chars, replacement->length);
        bufLen += replacement->length;
        cursor += end;

        if (bufLen >= 8000) break;
    }

    int remaining = (int)strlen(cursor);
    memcpy(buf + bufLen, cursor, remaining);
    bufLen += remaining;

    regfree(&reg);
    return OBJ_VAL(copyString(buf, bufLen));
}

static Value regexSplit(int argCount, Value *args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("Regex.split expects (pattern: String, text: String)");
        return NIL_VAL;
    }
    ObjString *pattern = AS_STRING(args[0]);
    ObjString *text = AS_STRING(args[1]);

    regex_t reg;
    if (regcomp(&reg, pattern->chars, REG_EXTENDED) != 0) {
        runtimeError("Invalid regex pattern: %s", pattern->chars);
        return NIL_VAL;
    }

    ObjList *results = newList();
    push(OBJ_VAL(results));

    const char *cursor = text->chars;
    regmatch_t pmatch;

    while (regexec(&reg, cursor, 1, &pmatch, 0) == 0) {
        int start = (int)pmatch.rm_so;
        int end = (int)pmatch.rm_eo;
        if (start == end) { cursor++; continue; }
        writeValueArray(&results->items, OBJ_VAL(copyString(cursor, start)));
        cursor += end;
    }
    writeValueArray(&results->items, OBJ_VAL(copyString(cursor, (int)strlen(cursor))));

    pop();
    regfree(&reg);
    return OBJ_VAL(results);
}

ObjModule *createRegexModule() {
    ObjModule *module = newModule("Regex", "regex", false);
    push(OBJ_VAL(module));
    defineModuleFunction(module, "match", regexMatch);
    defineModuleFunction(module, "find", regexFind);
    defineModuleFunction(module, "find_all", regexFindAll);
    defineModuleFunction(module, "replace", regexReplace);
    defineModuleFunction(module, "split", regexSplit);
    pop();
    return module;
}

SimpleType *createRegexModuleType() {
    SimpleType *mod = newSimpleType();
    createBuiltinFunctorType(mod, "match", (Type *[]) {(Type *) stringType, (Type *) stringType}, 2, NULL, 0, (Type *) boolType);
    createBuiltinFunctorType(mod, "find", (Type *[]) {(Type *) stringType, (Type *) stringType}, 2, NULL, 0, (Type *) anyType);
    createBuiltinFunctorType(mod, "find_all", (Type *[]) {(Type *) stringType, (Type *) stringType}, 2, NULL, 0, (Type *) anyType);
    createBuiltinFunctorType(mod, "replace", (Type *[]) {(Type *) stringType, (Type *) stringType, (Type *) stringType}, 3, NULL, 0, (Type *) stringType);
    createBuiltinFunctorType(mod, "split", (Type *[]) {(Type *) stringType, (Type *) stringType}, 2, NULL, 0, (Type *) anyType);
    return mod;
}

ModuleRegister regexModuleRegister = {
    createRegexModule,
    createRegexModuleType,
    "regex",
    "Regex",
    true
};
