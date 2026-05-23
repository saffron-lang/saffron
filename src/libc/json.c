#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "json.h"
#include "list.h"
#include "map.h"
#include "../memory.h"

// --- JSON Parser ---

typedef struct {
    const char *start;
    const char *current;
    bool hadError;
} JsonParser;

static void jsonError(JsonParser *p) {
    p->hadError = true;
}

static char jsonAdvance(JsonParser *p) {
    return *p->current++;
}

static char jsonPeek(JsonParser *p) {
    return *p->current;
}

static void jsonSkipWhitespace(JsonParser *p) {
    while (*p->current == ' ' || *p->current == '\t' ||
           *p->current == '\r' || *p->current == '\n') {
        p->current++;
    }
}

static bool jsonMatch(JsonParser *p, char c) {
    if (*p->current != c) return false;
    p->current++;
    return true;
}

static Value parseJsonValue(JsonParser *p);

static Value parseJsonString(JsonParser *p) {
    char buf[4096];
    int len = 0;

    while (*p->current != '"' && *p->current != '\0') {
        if (*p->current == '\\') {
            p->current++;
            switch (*p->current) {
                case '"': buf[len++] = '"'; break;
                case '\\': buf[len++] = '\\'; break;
                case '/': buf[len++] = '/'; break;
                case 'n': buf[len++] = '\n'; break;
                case 't': buf[len++] = '\t'; break;
                case 'r': buf[len++] = '\r'; break;
                case 'b': buf[len++] = '\b'; break;
                case 'f': buf[len++] = '\f'; break;
                default: buf[len++] = *p->current; break;
            }
        } else {
            buf[len++] = *p->current;
        }
        p->current++;
        if (len >= 4095) break;
    }

    if (*p->current != '"') { jsonError(p); return NIL_VAL; }
    p->current++; // consume closing "

    ObjString *str = copyString(buf, len);
    return OBJ_VAL(str);
}

static Value parseJsonNumber(JsonParser *p) {
    const char *start = p->current - 1;
    while (isdigit(*p->current) || *p->current == '.' ||
           *p->current == 'e' || *p->current == 'E' ||
           *p->current == '+' || *p->current == '-') {
        p->current++;
    }
    double num = strtod(start, NULL);
    return NUMBER_VAL(num);
}

static Value parseJsonArray(JsonParser *p) {
    ObjList *list = newList();
    push(OBJ_VAL(list));

    jsonSkipWhitespace(p);
    if (jsonPeek(p) == ']') {
        p->current++;
        return pop();
    }

    for (;;) {
        jsonSkipWhitespace(p);
        Value item = parseJsonValue(p);
        if (p->hadError) { pop(); return NIL_VAL; }
        push(item);
        listPush(list, item);
        pop();

        jsonSkipWhitespace(p);
        if (jsonMatch(p, ']')) break;
        if (!jsonMatch(p, ',')) { jsonError(p); pop(); return NIL_VAL; }
    }

    return pop();
}

static Value parseJsonObject(JsonParser *p) {
    ObjMap *map = newMap();
    push(OBJ_VAL(map));

    jsonSkipWhitespace(p);
    if (jsonPeek(p) == '}') {
        p->current++;
        return pop();
    }

    for (;;) {
        jsonSkipWhitespace(p);
        if (!jsonMatch(p, '"')) { jsonError(p); pop(); return NIL_VAL; }
        Value key = parseJsonString(p);
        if (p->hadError) { pop(); return NIL_VAL; }

        jsonSkipWhitespace(p);
        if (!jsonMatch(p, ':')) { jsonError(p); pop(); return NIL_VAL; }

        jsonSkipWhitespace(p);
        Value val = parseJsonValue(p);
        if (p->hadError) { pop(); return NIL_VAL; }

        push(key);
        push(val);
        valueTableSet(&map->values, key, val);
        pop();
        pop();

        jsonSkipWhitespace(p);
        if (jsonMatch(p, '}')) break;
        if (!jsonMatch(p, ',')) { jsonError(p); pop(); return NIL_VAL; }
    }

    return pop();
}

static Value parseJsonValue(JsonParser *p) {
    jsonSkipWhitespace(p);
    char c = jsonAdvance(p);

    switch (c) {
        case '"': return parseJsonString(p);
        case '{': return parseJsonObject(p);
        case '[': return parseJsonArray(p);
        case 't':
            if (strncmp(p->current, "rue", 3) == 0) { p->current += 3; return BOOL_VAL(true); }
            jsonError(p); return NIL_VAL;
        case 'f':
            if (strncmp(p->current, "alse", 4) == 0) { p->current += 4; return BOOL_VAL(false); }
            jsonError(p); return NIL_VAL;
        case 'n':
            if (strncmp(p->current, "ull", 3) == 0) { p->current += 3; return NIL_VAL; }
            jsonError(p); return NIL_VAL;
        default:
            if (c == '-' || isdigit(c)) return parseJsonNumber(p);
            jsonError(p); return NIL_VAL;
    }
}

// --- Stringify ---

typedef struct {
    char *buf;
    int len;
    int cap;
} StringBuilder;

static void sbInit(StringBuilder *sb) {
    sb->cap = 256;
    sb->len = 0;
    sb->buf = malloc(sb->cap);
}

static void sbAppend(StringBuilder *sb, const char *str, int strLen) {
    while (sb->len + strLen >= sb->cap) {
        sb->cap *= 2;
        sb->buf = realloc(sb->buf, sb->cap);
    }
    memcpy(sb->buf + sb->len, str, strLen);
    sb->len += strLen;
}

static void sbAppendChar(StringBuilder *sb, char c) {
    sbAppend(sb, &c, 1);
}

static void stringifyValue(Value value, StringBuilder *sb);

static void stringifyString(ObjString *str, StringBuilder *sb) {
    sbAppendChar(sb, '"');
    for (int i = 0; i < str->length; i++) {
        char c = str->chars[i];
        switch (c) {
            case '"': sbAppend(sb, "\\\"", 2); break;
            case '\\': sbAppend(sb, "\\\\", 2); break;
            case '\n': sbAppend(sb, "\\n", 2); break;
            case '\t': sbAppend(sb, "\\t", 2); break;
            case '\r': sbAppend(sb, "\\r", 2); break;
            default: sbAppendChar(sb, c); break;
        }
    }
    sbAppendChar(sb, '"');
}

static void stringifyValue(Value value, StringBuilder *sb) {
    if (IS_NIL(value)) {
        sbAppend(sb, "null", 4);
    } else if (IS_BOOL(value)) {
        if (AS_BOOL(value)) sbAppend(sb, "true", 4);
        else sbAppend(sb, "false", 5);
    } else if (IS_NUMBER(value)) {
        char num[64];
        int len = snprintf(num, sizeof(num), "%g", AS_NUMBER(value));
        sbAppend(sb, num, len);
    } else if (IS_STRING(value)) {
        stringifyString(AS_STRING(value), sb);
    } else if (IS_OBJ(value)) {
        Obj *obj = AS_OBJ(value);
        if (obj->type == OBJ_LIST) {
            ObjList *list = (ObjList *) obj;
            sbAppendChar(sb, '[');
            for (int i = 0; i < list->items.count; i++) {
                if (i > 0) sbAppendChar(sb, ',');
                stringifyValue(list->items.values[i], sb);
            }
            sbAppendChar(sb, ']');
        } else if (obj->type == OBJ_MAP) {
            ObjMap *map = (ObjMap *) obj;
            sbAppendChar(sb, '{');
            bool first = true;
            for (int i = 0; i < map->values.capacity; i++) {
                MapEntry *entry = &map->values.entries[i];
                if (IS_NIL(entry->key) && IS_NIL(entry->value)) continue;
                if (IS_NIL(entry->key)) continue;
                if (!first) sbAppendChar(sb, ',');
                first = false;
                if (IS_STRING(entry->key)) {
                    stringifyString(AS_STRING(entry->key), sb);
                } else {
                    sbAppendChar(sb, '"');
                    char tmp[64];
                    int len = snprintf(tmp, sizeof(tmp), "%g", AS_NUMBER(entry->key));
                    sbAppend(sb, tmp, len);
                    sbAppendChar(sb, '"');
                }
                sbAppendChar(sb, ':');
                stringifyValue(entry->value, sb);
            }
            sbAppendChar(sb, '}');
        } else if (obj->type == OBJ_INSTANCE) {
            ObjInstance *instance = (ObjInstance *) obj;
            ObjClass *klass = instance->klass;
            if (klass->isDataClass && klass->fieldMetas.count > 0) {
                sbAppendChar(sb, '{');
                for (int i = 0; i < klass->fieldMetas.count; i++) {
                    if (i > 0) sbAppendChar(sb, ',');
                    FieldMeta *meta = &klass->fieldMetas.entries[i];
                    stringifyString(meta->name, sb);
                    sbAppendChar(sb, ':');
                    Value fieldVal;
                    if (tableGet(&instance->fields, meta->name, &fieldVal)) {
                        stringifyValue(fieldVal, sb);
                    } else {
                        sbAppend(sb, "null", 4);
                    }
                }
                sbAppendChar(sb, '}');
            } else {
                sbAppend(sb, "null", 4);
            }
        } else {
            sbAppend(sb, "null", 4);
        }
    }
}

// --- Native Functions ---

static Value jsonParseNative(int argCount, Value *args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("Json.parse expects 1 string argument");
        return NIL_VAL;
    }
    ObjString *str = AS_STRING(args[0]);
    JsonParser parser = { str->chars, str->chars, false };
    Value result = parseJsonValue(&parser);
    if (parser.hadError) {
        runtimeError("JSON parse error");
        return NIL_VAL;
    }
    return result;
}

static Value jsonParseIntoNative(int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError("Json.parse_into expects 2 arguments (class, string)");
        return NIL_VAL;
    }
    if (!IS_CLASS(args[0])) {
        runtimeError("Json.parse_into: first argument must be a class");
        return NIL_VAL;
    }
    if (!IS_STRING(args[1])) {
        runtimeError("Json.parse_into: second argument must be a string");
        return NIL_VAL;
    }

    ObjClass *klass = AS_CLASS(args[0]);
    if (!klass->isDataClass) {
        runtimeError("Json.parse_into requires a data class");
        return NIL_VAL;
    }

    ObjString *jsonStr = AS_STRING(args[1]);
    JsonParser parser = { jsonStr->chars, jsonStr->chars, false };

    jsonSkipWhitespace(&parser);
    if (!jsonMatch(&parser, '{')) {
        runtimeError("Json.parse_into: expected JSON object");
        return NIL_VAL;
    }

    Value parsed = parseJsonObject(&parser);
    if (parser.hadError) {
        runtimeError("Json.parse_into: JSON parse error");
        return NIL_VAL;
    }

    ObjMap *map = (ObjMap *) AS_OBJ(parsed);
    ObjInstance *instance = newInstance(klass);
    push(OBJ_VAL(instance));

    for (int i = 0; i < klass->fieldMetas.count; i++) {
        FieldMeta *meta = &klass->fieldMetas.entries[i];
        Value fieldVal;
        if (valueTableGet(&map->values, OBJ_VAL(meta->name), &fieldVal)) {
            tableSet(&instance->fields, meta->name, fieldVal);
        }
    }

    return pop();
}

static Value jsonStringifyNative(int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError("Json.stringify expects 1 argument");
        return NIL_VAL;
    }

    StringBuilder sb;
    sbInit(&sb);
    stringifyValue(args[0], &sb);

    ObjString *result = copyString(sb.buf, sb.len);
    free(sb.buf);
    return OBJ_VAL(result);
}

// --- Module Registration ---

ObjModule *createJsonModule() {
    ObjModule *module = newModule("Json", "json", false);
    push(OBJ_VAL(module));
    defineModuleFunction(module, "parse", jsonParseNative);
    defineModuleFunction(module, "parse_into", jsonParseIntoNative);
    defineModuleFunction(module, "stringify", jsonStringifyNative);
    pop();
    return module;
}

SimpleType *createJsonModuleType() {
    SimpleType *jsonModule = newSimpleType();
    createBuiltinFunctorType(jsonModule, "parse", (Type *[]) {(Type *) stringType}, 1, NULL, 0, (Type *) anyType);
    createBuiltinFunctorType(jsonModule, "parse_into", (Type *[]) {(Type *) anyType, (Type *) stringType}, 2, NULL, 0, (Type *) anyType);
    createBuiltinFunctorType(jsonModule, "stringify", (Type *[]) {(Type *) anyType}, 1, NULL, 0, (Type *) stringType);
    return jsonModule;
}

ModuleRegister jsonModuleRegister = {
    createJsonModule,
    createJsonModuleType,
    "json",
    "Json",
    true
};
